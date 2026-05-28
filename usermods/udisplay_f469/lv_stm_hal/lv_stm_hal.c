#include "lv_stm_hal.h"
#include "lv_conf.h"
#include "lvgl/src/display/lv_display.h"
#include "lvgl/src/indev/lv_indev.h"
#include "lvgl/src/core/lv_refr.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_ts.h"
#include "sdram_layout.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static void tft_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/* Transition compositor
 *
 * Outside transitions LVGL renders into its RGB565 draw buffer in SRAM, and
 * DMA2D converts and copies each flush into the live framebuffer, which LTDC
 * scans. The three framebuffers come from sdram_layout.h.
 *
 * A transition animates a rectangle from the current screen (OLD) to a new
 * one (NEW) without LVGL drawing a single frame of the animation:
 *
 *   1. tft_transition_start() renders NEW once into FB2 by redirecting the
 *      flush, and copies NEW's area outside the rectangle (status bar,
 *      navigation bar) into both other framebuffers.
 *   2. The other two framebuffers alternate: LTDC scans one (b_scan) while
 *      the next frame is composed into the other (b_next). Each frame takes
 *      at most two DMA2D copies into b_next: the part of OLD that is still
 *      visible, read from the previous frame in b_scan at its shifted
 *      position, and the visible part of NEW from FB2. OLD only ever moves
 *      in one direction, so every OLD pixel a frame needs is still in the
 *      previous frame.
 *   3. When both copies are done, the LTDC line-0 interrupt queues the
 *      buffer swap for the next vertical blanking. The reload interrupt then
 *      swaps the roles and composes the next frame. The offset follows the
 *      elapsed time through the selected easing curve, and the last frame
 *      lands exactly on the full extent.
 *   4. While a transition runs, LVGL flushes are dropped; the done callback
 *      is the caller's cue to invalidate the screen.
 *
 * After tft_transition_start() returns, everything runs in the DMA2D and
 * LTDC interrupts.
 *
 * Example, horizontal push-in at offset d (previous frame at d_prev,
 * rectangle width W): columns [0, W-d) of b_next are OLD, read from b_scan
 * starting at column d-d_prev; columns [W-d, W) are NEW, read from FB2
 * starting at column 0.
 */

/* DMA2D state machine */
#define XFER_IDLE        0
#define XFER_FLUSH       1   /* DMA2D busy with an LVGL flush               */
#define XFER_COMPOSE     2   /* DMA2D busy with a compositor step; the
                              * specific step lives in s_compose_step.      */

/* Per-tick compositor steps, advanced one by one within a frame. The area
 * outside the rectangle is copied once by tft_transition_start, not per
 * frame. */
#define CSTEP_SLICE_A      0  /* OLD slice from B_scan into rect (shifted)  */
#define CSTEP_SLICE_B      1  /* NEW slice from B_new  into rect            */
#define CSTEP_DONE         2  /* tick complete -> arm VBLANK swap           */

/* The buffer LTDC is currently scanning. LVGL flushes write here unless
 * a render-override is active (set by the compositor during the offscreen
 * NEW-screen render). */
static volatile uint32_t s_live_fb = SDRAM_FB0_ADDR;

/* When non-zero, tft_flush writes here instead of s_live_fb and skips the
 * compositor's flush_is_last bookkeeping (offscreen render path). */
static volatile uint32_t s_flush_override = 0;

/* DMA2D state */
static volatile uint8_t  s_xfer_state    = XFER_IDLE;
static volatile lv_display_t *s_disp_in_flight = NULL;

/* Compositor state (valid only while s_anim_active != 0). */
static volatile uint8_t  s_anim_active   = 0;
static volatile uint8_t  s_anim_type     = 0;
static volatile uint32_t s_anim_start_ms = 0;
static volatile uint32_t s_anim_dur_ms   = 0;
static volatile uint32_t s_anim_b_scan   = 0; /* current composite / OLD-at-start */
static volatile uint32_t s_anim_b_next   = 0; /* next composite target */
static volatile uint32_t s_anim_b_new    = 0; /* pinned NEW source (FB2) */
static volatile uint32_t s_anim_d_prev   = 0; /* d at which B_scan was composed */
static volatile uint32_t s_anim_d_now    = 0; /* d the current tick is composing */
static volatile uint16_t s_anim_rect_x   = 0;
static volatile uint16_t s_anim_rect_y   = 0;
static volatile uint16_t s_anim_rect_w   = 0;
static volatile uint16_t s_anim_rect_h   = 0;
static volatile uint8_t  s_anim_final    = 0; /* set when the in-flight compose
                                                 is the t=1.0 (final) frame */
static volatile uint8_t  s_anim_easing   = TFT_EASING_LINEAR; /* selected curve */
/* Per-tick step bookkeeping. */
static volatile uint8_t  s_compose_step   = CSTEP_DONE;
static void (*s_anim_on_done_cb)(void *) = NULL;
static void *s_anim_on_done_arg = NULL;

/* A swap arming used solely by the compositor: at the next LTDC line-0,
 * if this is set we queue a VBLANK reload to s_anim_b_next. */
static volatile uint8_t  s_compose_swap_pending = 0;

/* ============== DMA2D / LTDC ISRs ===================================== */

static void compose_kick_tick(uint32_t now_ms);
static void compose_run(uint8_t starting_step);
static void compose_abort(void);

static void tft_dma2d_xfer_cplt_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    uint8_t st = s_xfer_state;
    if (st == XFER_FLUSH) {
        lv_display_t *disp = (lv_display_t *)s_disp_in_flight;
        s_disp_in_flight = NULL;
        s_xfer_state = XFER_IDLE;
        if (disp) lv_display_flush_ready(disp); /* LV_USE_OS=0: ISR safe */
    } else if (st == XFER_COMPOSE) {
        /* Continue the per-tick chain at the next step. */
        compose_run((uint8_t)(s_compose_step + 1));
    }
}

static void tft_dma2d_xfer_err_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    lv_display_t *disp = (lv_display_t *)s_disp_in_flight;
    s_disp_in_flight = NULL;
    s_xfer_state = XFER_IDLE;
    s_compose_swap_pending = 0;
    /* Best-effort: also abort any in-flight animation. */
    if (s_anim_active) {
        s_anim_active = 0;
        if (s_anim_on_done_cb) s_anim_on_done_cb(s_anim_on_done_arg);
    }
    if (disp) lv_display_flush_ready(disp);
}

void DMA2D_IRQHandler(void) {
    HAL_DMA2D_IRQHandler(&hdma2d_eval);
}

void LTDC_IRQHandler(void) {
    HAL_LTDC_IRQHandler(&hltdc_eval);
}

/* Line-event IRQ at line 0 of every frame. Only the compositor arms swaps
 * (LVGL flushes write directly to the live FB and never trigger this). */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc) {
    if (s_compose_swap_pending && s_xfer_state == XFER_IDLE) {
        s_compose_swap_pending = 0;
        HAL_LTDC_SetAddress_NoReload(hltdc, s_anim_b_next,
                                     LTDC_ACTIVE_LAYER_BACKGROUND);
        HAL_LTDC_Reload(hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
    }
    HAL_LTDC_ProgramLineEvent(hltdc, 0);
}

/* Reload-event IRQ fires at VBLANK once the queued address reload has been
 * applied. LTDC is now scanning the buffer we previously called B_next. */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc) {
    (void)hltdc;
    /* Old B_scan rotates into B_next; B_next becomes B_scan. */
    uint32_t old_scan = s_anim_b_scan;
    s_anim_b_scan = s_anim_b_next;
    s_anim_b_next = old_scan;
    s_anim_d_prev = s_anim_d_now;

    /* The live FB tracks LTDC's current source. */
    s_live_fb = s_anim_b_scan;

    if (s_anim_active) {
        if (s_anim_final) {
            /* Just displayed the t=1.0 frame; animation done. */
            s_anim_active = 0;
            s_anim_final  = 0;
            if (s_anim_on_done_cb) s_anim_on_done_cb(s_anim_on_done_arg);
        } else {
            compose_kick_tick(HAL_GetTick());
        }
    }
}

/* ============== Compositor tick ====================================== */

static void compose_abort(void) {
    s_xfer_state = XFER_IDLE;
    s_compose_step = CSTEP_DONE;
    s_compose_swap_pending = 0;
    s_anim_active = 0;
    if (s_anim_on_done_cb) s_anim_on_done_cb(s_anim_on_done_arg);
}

/* Run the per-tick step chain starting at `starting_step`. Each step
 * either issues an IT DMA2D copy and returns (the completion callback
 * will continue with starting_step+1) or is empty and we fall through
 * to the next step synchronously. After CSTEP_SLICE_B we arm the
 * VBLANK swap.
 *
 * All step parameters are read from globals (rect, b_*, d) which have
 * been set up by compose_kick_tick before the first call.
 */
static void compose_run(uint8_t starting_step) {
    uint32_t rx = s_anim_rect_x;
    uint32_t ry = s_anim_rect_y;
    uint32_t rw = s_anim_rect_w;
    uint32_t rh = s_anim_rect_h;
    uint32_t d      = s_anim_d_now;
    uint32_t d_prev = s_anim_d_prev;

    /* Per-tick slice rects. SLICE_A reads from b_scan (the previous
     * composition), SLICE_B reads from b_new (pinned NEW). Both write
     * into b_next. Geometry depends on animation type. */
    uint32_t a_sx = 0, a_sy = 0, a_dx = 0, a_dy = 0, a_w = 0, a_h = 0;
    uint32_t b_sx = 0, b_sy = 0, b_dx = 0, b_dy = 0, b_w = 0, b_h = 0;

    switch (s_anim_type) {
    case TFT_ANIM_HORIZONTAL_PUSH_IN: {
        /* OLD shifts LEFT by d, NEW arrives from RIGHT. */
        uint32_t delta = (d >= d_prev) ? (d - d_prev) : 0u;
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        a_sx = rx + delta; a_sy = ry; a_dx = rx;             a_dy = ry; a_w = old_w; a_h = rh;
        b_sx = rx;         b_sy = ry; b_dx = rx + old_w;     b_dy = ry; b_w = d;     b_h = rh;
        break;
    }
    case TFT_ANIM_HORIZONTAL_PUSH_OUT: {
        /* OLD shifts RIGHT by d, NEW arrives from LEFT. */
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        a_sx = rx + d_prev;     a_sy = ry; a_dx = rx + d; a_dy = ry; a_w = old_w; a_h = rh;
        b_sx = rx + old_w;      b_sy = ry; b_dx = rx;     b_dy = ry; b_w = d;     b_h = rh;
        break;
    }
    case TFT_ANIM_HORIZONTAL_SLIDE_IN: {
        /* NEW slides LEFT from off-screen-right over stationary OLD.
         * At tick d:
         *   viewport [rx, rx+rw-d)  = OLD's left (rw-d) columns (stationary)
         *   viewport [rx+rw-d, rx+rw) = NEW's left d columns (leading
         *                               edge of NEW emerging from right) */
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        a_sx = rx;          a_sy = ry; a_dx = rx;          a_dy = ry; a_w = old_w; a_h = rh;
        b_sx = rx;          b_sy = ry; b_dx = rx + old_w;  b_dy = ry; b_w = d;     b_h = rh;
        break;
    }
    case TFT_ANIM_HORIZONTAL_SLIDE_OUT: {
        /* OLD shifts RIGHT off-screen; NEW revealed underneath from LEFT.
         * OLD pixels in b_scan are at [rx + d_prev, rx + rw); we need
         * them at [rx + d, rx + rw) in b_next. */
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        a_sx = rx + d_prev; a_sy = ry; a_dx = rx + d; a_dy = ry; a_w = old_w; a_h = rh;
        b_sx = rx;          b_sy = ry; b_dx = rx;     b_dy = ry; b_w = d;     b_h = rh;
        break;
    }
    case TFT_ANIM_VERTICAL_SLIDE_IN: {
        /* NEW slides UP from below over stationary OLD. At tick d:
         *   viewport [ry, ry+rh-d) = OLD top (rh-d) rows (stationary)
         *   viewport [ry+rh-d, ry+rh) = NEW top d rows (leading edge of
         *                               NEW emerging from bottom)
         * `d` is vertical extent here. */
        uint32_t old_h = (d <= rh) ? (rh - d) : 0u;
        a_sx = rx; a_sy = ry;          a_dx = rx; a_dy = ry;          a_w = rw; a_h = old_h;
        b_sx = rx; b_sy = ry;          b_dx = rx; b_dy = ry + old_h;  b_w = rw; b_h = d;
        break;
    }
    case TFT_ANIM_VERTICAL_SLIDE_OUT: {
        /* OLD shifts DOWN off-screen; NEW revealed underneath from TOP. */
        uint32_t old_h = (d <= rh) ? (rh - d) : 0u;
        a_sx = rx; a_sy = ry + d_prev; a_dx = rx; a_dy = ry + d; a_w = rw; a_h = old_h;
        b_sx = rx; b_sy = ry;          b_dx = rx; b_dy = ry;     b_w = rw; b_h = d;
        break;
    }
    default:
        compose_abort();
        return;
    }

    uint8_t step = starting_step;
    while (step < CSTEP_DONE) {
        s_compose_step = step;
        switch (step) {
        case CSTEP_SLICE_A:
            /* OLD region: copy from b_scan into b_next. */
            if (a_w > 0 && a_h > 0) {
                s_xfer_state = XFER_COMPOSE;
                if (BSP_LCD_CopyRectEx_IT(s_anim_b_scan, a_sx, a_sy,
                                          s_anim_b_next, a_dx, a_dy,
                                          a_w, a_h) != LCD_OK) {
                    compose_abort();
                    return;
                }
                return;
            }
            break;
        case CSTEP_SLICE_B:
            /* NEW region: copy from b_new (pinned NEW) into b_next. */
            if (b_w > 0 && b_h > 0) {
                s_xfer_state = XFER_COMPOSE;
                if (BSP_LCD_CopyRectEx_IT(s_anim_b_new,  b_sx, b_sy,
                                          s_anim_b_next, b_dx, b_dy,
                                          b_w, b_h) != LCD_OK) {
                    compose_abort();
                    return;
                }
                return;
            }
            break;
        default:
            break;
        }
        step++;
    }
    /* All steps complete (or all empty) -> arm VBLANK swap. */
    s_compose_step = CSTEP_DONE;
    s_xfer_state = XFER_IDLE;
    s_compose_swap_pending = 1;
}

/* ============== Easing curves =======================================
 *
 * Each curve maps t in 0..1024 (linear normalised elapsed time) to an
 * eased value in 0..1024. Curves must be monotone non-decreasing and
 * satisfy f(0) == 0, f(1024) == 1024 so the compositor's monotonic
 * pixel-offset clamp never trims the animation and so the final frame
 * is always exactly at the requested rect extent.
 *
 * To add a curve: write a `static uint32_t tft_ease_<name>(uint32_t)`
 * here, append the matching TFT_EASING_<NAME> id in lv_stm_hal.h, and
 * add the pointer to s_easing_table[] at the same index.
 *
 * Implementation notes:
 *   - Everything runs in the LTDC reload IRQ. No heap, no divisions
 *     (a single >> 10 per multiplication is the price of 1024-scale
 *     fixed point). Cubic costs three multiplies and one shift.
 *   - All curves are written to be safe at the endpoints without a
 *     branch: at t=0 the result is exactly 0, at t=1024 exactly 1024.
 */

typedef uint32_t (*tft_easing_fn_t)(uint32_t t_q);

static uint32_t tft_ease_linear(uint32_t t_q) {
    return t_q;
}

static uint32_t tft_ease_in_cubic(uint32_t t_q) {
    /* f(t) = t^3 */
    uint32_t t2 = (t_q * t_q) >> 10;
    return (t2 * t_q) >> 10;
}

static uint32_t tft_ease_out_cubic(uint32_t t_q) {
    /* f(t) = 1 - (1-t)^3 */
    uint32_t u  = 1024u - t_q;
    uint32_t u2 = (u * u) >> 10;
    uint32_t u3 = (u2 * u) >> 10;
    return 1024u - u3;
}

static uint32_t tft_ease_in_out_cubic(uint32_t t_q) {
    /* f(t) = 4 t^3                    for t < 0.5
       f(t) = 1 - (-2t + 2)^3 / 2      for t >= 0.5 */
    if (t_q < 512u) {
        uint32_t t2 = (t_q * t_q) >> 10;
        uint32_t t3 = (t2 * t_q) >> 10;
        return t3 * 4u;
    } else {
        uint32_t u  = (1024u - t_q) * 2u;       /* 2 - 2t scaled to 0..1024 */
        uint32_t u2 = (u * u) >> 10;
        uint32_t u3 = (u2 * u) >> 10;
        return 1024u - (u3 >> 1);
    }
}

static uint32_t tft_ease_out_quint(uint32_t t_q) {
    /* f(t) = 1 - (1-t)^5  -- a stronger ease-out than cubic */
    uint32_t u  = 1024u - t_q;
    uint32_t u2 = (u  * u ) >> 10;
    uint32_t u4 = (u2 * u2) >> 10;
    uint32_t u5 = (u4 * u ) >> 10;
    return 1024u - u5;
}

/* Index matches TFT_EASING_* ids in lv_stm_hal.h. */
static const tft_easing_fn_t s_easing_table[TFT_EASING__COUNT] = {
    [TFT_EASING_LINEAR]            = tft_ease_linear,
    [TFT_EASING_EASE_IN_CUBIC]     = tft_ease_in_cubic,
    [TFT_EASING_EASE_OUT_CUBIC]    = tft_ease_out_cubic,
    [TFT_EASING_EASE_IN_OUT_CUBIC] = tft_ease_in_out_cubic,
    [TFT_EASING_EASE_OUT_QUINT]    = tft_ease_out_quint,
};

static inline uint32_t apply_easing(uint32_t t_q, uint8_t kind) {
    if (kind >= TFT_EASING__COUNT) return t_q;
    tft_easing_fn_t fn = s_easing_table[kind];
    return fn ? fn(t_q) : t_q;
}

static void compose_kick_tick(uint32_t now_ms) {
    uint8_t type = s_anim_type;
    uint8_t is_vertical = (type == TFT_ANIM_VERTICAL_SLIDE_IN ||
                           type == TFT_ANIM_VERTICAL_SLIDE_OUT);
    uint32_t extent = is_vertical ? (uint32_t)s_anim_rect_h
                                  : (uint32_t)s_anim_rect_w;

    uint32_t elapsed = now_ms - s_anim_start_ms;
    uint32_t t_q;  /* 0..1024 fixed point */
    uint8_t  is_final = 0;
    if (elapsed >= s_anim_dur_ms) {
        t_q = 1024;
        is_final = 1;
    } else {
        t_q = (elapsed * 1024u) / s_anim_dur_ms;
    }
    s_anim_final = is_final;

    if (type < TFT_ANIM_HORIZONTAL_SLIDE_IN ||
        type > TFT_ANIM_VERTICAL_SLIDE_OUT) {
        compose_abort();
        return;
    }

    /* Map linear time to eased position. Both axes are 0..1024 fixed
     * point; see TFT_EASING_* / s_easing_table[] above. The final tick
     * (is_final) always lands at 1024 so the rect ends exactly at full
     * extent regardless of the chosen curve. */
    uint32_t te_q = is_final ? 1024u : apply_easing(t_q, s_anim_easing);
    uint32_t d = (extent * te_q) / 1024u;
    if (d < s_anim_d_prev) d = s_anim_d_prev; /* monotonic safety */
    s_anim_d_now = d;

    compose_run(CSTEP_SLICE_A);
}

/* Copy the four outside-rect strips (top/bottom/left/right of the current
 * animation rect) from b_new into dst_fb using BLOCKING DMA2D. Used to
 * seed both rotation slots' outside-rect (nav bar, status bar, ...)
 * with the NEW frame before the animation starts, so neither slot ever
 * shows OLD outside-rect pixels during the slide. */
static void seed_outside_rect_blocking(uint32_t dst_fb) {
    uint32_t W  = LV_HOR_RES_MAX;
    uint32_t H  = LV_VER_RES_MAX;
    uint32_t rx = s_anim_rect_x;
    uint32_t ry = s_anim_rect_y;
    uint32_t rw = s_anim_rect_w;
    uint32_t rh = s_anim_rect_h;

    if (ry > 0) {
        (void)BSP_LCD_CopyRectEx(s_anim_b_new, 0, 0,
                                 dst_fb,       0, 0,
                                 W, ry);
    }
    {
        uint32_t by = ry + rh;
        uint32_t bh = (by < H) ? (H - by) : 0u;
        if (bh > 0) {
            (void)BSP_LCD_CopyRectEx(s_anim_b_new, 0, by,
                                     dst_fb,       0, by,
                                     W, bh);
        }
    }
    if (rx > 0) {
        (void)BSP_LCD_CopyRectEx(s_anim_b_new, 0, ry,
                                 dst_fb,       0, ry,
                                 rx, rh);
    }
    {
        uint32_t rrx = rx + rw;
        uint32_t rrw = (rrx < W) ? (W - rrx) : 0u;
        if (rrw > 0) {
            (void)BSP_LCD_CopyRectEx(s_anim_b_new, rrx, ry,
                                     dst_fb,       rrx, ry,
                                     rrw, rh);
        }
    }
}

/* ============== Public transition API ================================ */

int tft_transition_active(void) {
    return s_anim_active ? 1 : 0;
}

int tft_transition_start(int anim_type, uint32_t duration_ms,
                         uint16_t rect_x, uint16_t rect_y,
                         uint16_t rect_w, uint16_t rect_h,
                         uint8_t easing,
                         void (*on_done)(void *), void *arg) {
    if (s_anim_active) return -1;
    if (anim_type < TFT_ANIM_HORIZONTAL_SLIDE_IN ||
        anim_type > TFT_ANIM_VERTICAL_SLIDE_OUT) return -1;
    if (easing >= TFT_EASING__COUNT) return -1;
    if (duration_ms == 0) duration_ms = 1;
    /* Clamp / validate rect. */
    if (rect_w == 0 || rect_h == 0) return -1;
    if ((uint32_t)rect_x + (uint32_t)rect_w > (uint32_t)LV_HOR_RES_MAX) return -1;
    if ((uint32_t)rect_y + (uint32_t)rect_h > (uint32_t)LV_VER_RES_MAX) return -1;

    /* 1. Wait for any in-flight LVGL flush to drain. */
    while (s_xfer_state != XFER_IDLE) { /* spin */ }

    /* 2. Decide slot roles. b_scan = current live FB; b_next = the other
     *    rotation slot (FB0/FB1); b_new = FB2. */
    s_anim_b_scan = s_live_fb;
    s_anim_b_next = (s_live_fb == SDRAM_FB0_ADDR) ? SDRAM_FB1_ADDR : SDRAM_FB0_ADDR;
    s_anim_b_new  = SDRAM_FB2_ADDR;
    s_anim_d_prev = 0;
    s_anim_d_now  = 0;
    s_anim_rect_x = rect_x;
    s_anim_rect_y = rect_y;
    s_anim_rect_w = rect_w;
    s_anim_rect_h = rect_h;

    /* 3. Render the NEW screen offscreen into FB2 via the flush-override.
     *    The caller is responsible for having scr_load'd / invalidated the
     *    new screen before this call, so the active LVGL display thinks
     *    NEW is the current screen and lv_refr_now will draw it. */
    s_flush_override = SDRAM_FB2_ADDR;
    lv_refr_now(NULL);
    s_flush_override = 0;
    while (s_xfer_state != XFER_IDLE) { /* spin */ }

    /* 3b. Seed BOTH rotation slots' outside-rect with NEW pixels before
     *     the animation starts so neither slot ever shows OLD nav bar /
     *     status bar / etc. during the slide.
     *     - b_next (off-screen): full blocking copy from b_new. Safe.
     *     - b_scan (currently live): strip-only copy of the outside-rect
     *       (top/bottom/left/right of the rect) from b_new. This writes
     *       to the FB LTDC is scanning, so in the worst case we get a
     *       single-frame tear on those strips. That is far less
     *       obtrusive than 200ms of nav-bar flicker afterwards. */
    (void)BSP_LCD_CopyRectEx(s_anim_b_new, 0, 0,
                             s_anim_b_next, 0, 0,
                             LV_HOR_RES_MAX, LV_VER_RES_MAX);
    seed_outside_rect_blocking(s_anim_b_scan);

    /* 4. Wire callback and start the animation. */
    s_anim_type      = (uint8_t)anim_type;
    s_anim_easing    = easing;
    s_anim_start_ms  = HAL_GetTick();
    s_anim_dur_ms    = duration_ms;
    s_anim_on_done_cb  = on_done;
    s_anim_on_done_arg = arg;
    s_anim_final     = 0;
    s_anim_active    = 1;

    /* 5. Kick the first compose tick. */
    compose_kick_tick(s_anim_start_ms);
    return 0;
}

/* ============== Init + per-frame flush =============================== */

void tft_init(void) {
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LTDC_ACTIVE_LAYER_BACKGROUND, SDRAM_FB0_ADDR);
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_BACKGROUND);
    BSP_LCD_Clear(0xFFFFFFFF);
    BSP_LCD_SetBackColor(0xFFFFFFFF);

    /* Seed FB1 and FB2 white so any compositor stage that samples them
     * before being legitimately populated reads white, not SDRAM garbage. */
    {
        const uint32_t *src = (const uint32_t *)SDRAM_FB0_ADDR;
        uint32_t *dst1 = (uint32_t *)SDRAM_FB1_ADDR;
        uint32_t *dst2 = (uint32_t *)SDRAM_FB2_ADDR;
        for (uint32_t i = 0; i < SDRAM_FB_BYTES / 4u; ++i) {
            uint32_t v = src[i];
            dst1[i] = v;
            dst2[i] = v;
        }
    }

    s_live_fb = SDRAM_FB0_ADDR;

    /* DMA2D completion callbacks. */
    hdma2d_eval.XferCpltCallback  = tft_dma2d_xfer_cplt_cb;
    hdma2d_eval.XferErrorCallback = tft_dma2d_xfer_err_cb;

    /* LTDC reload IRQ + line-0 IRQ used only by the compositor. */
    __HAL_LTDC_ENABLE_IT(&hltdc_eval, LTDC_IT_RR);
    HAL_LTDC_ProgramLineEvent(&hltdc_eval, 0);

    static uint16_t buf1[LV_HOR_RES_MAX * 60];
    lv_display_t *disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, tft_flush);
    lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

static void tft_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    if (area->x2 < area->x1 || area->y2 < area->y1 || !px_map) {
        lv_display_flush_ready(disp);
        return;
    }

    /* During a transition, flushes are dropped. The LTDC reload interrupt
     * can start compositor copies at any time, and a flush would compete
     * with them for DMA2D and for s_xfer_state; its completion could then
     * be taken for a compositor step and leave LVGL waiting forever. The
     * new screen is already rendered, and the done callback invalidates
     * it, so whatever changed meanwhile is flushed afterwards. The
     * offscreen render in tft_transition_start() uses s_flush_override
     * and runs before s_anim_active is set. */
    if (s_anim_active && !s_flush_override) {
        lv_display_flush_ready(disp);
        return;
    }

    /* Serialize with any in-flight DMA2D operation (LVGL flush or
     * compositor). The flush callback runs in main-loop context so a
     * brief busy-wait is safe. */
    while (s_xfer_state != XFER_IDLE) { /* spin */ }

    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    /* Write target: explicit override (offscreen NEW render in
     * tft_transition_start) or the currently scanned live FB. Flushes
     * during s_anim_active were already short-circuited above. */
    uint32_t dst_fb = s_flush_override ? s_flush_override : s_live_fb;

    /* LVGL v9 aligns rendered row strides to LV_DRAW_BUF_STRIDE_ALIGN
     * bytes. For RGB565 (2 bytes/px) on a 4-byte alignment, odd widths
     * acquire one padding pixel per row. Pass the actual pitch to DMA2D
     * so it skips the padding; otherwise rows progressively shift and
     * the rendered region appears diagonally skewed. */
    uint32_t row_bytes = (w * 2u + (LV_DRAW_BUF_STRIDE_ALIGN - 1u))
                         & ~(uint32_t)(LV_DRAW_BUF_STRIDE_ALIGN - 1u);
    uint32_t src_pitch_px = row_bytes / 2u;

    s_disp_in_flight = disp;
    s_xfer_state = XFER_FLUSH;

    uint8_t result = BSP_LCD_DrawBitmapRaw_IT_To(dst_fb,
                                                  area->x1, area->y1, w, h,
                                                  16, px_map, src_pitch_px);
    if (result != LCD_OK) {
        s_xfer_state = XFER_IDLE;
        s_disp_in_flight = NULL;
        lv_display_flush_ready(disp);
        return;
    }
    /* Completion via DMA2D XferCplt -> lv_display_flush_ready. */
}

/**************** touchpad ****************/

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
static TS_StateTypeDef  TS_State;

void touchpad_init(void) {
    BSP_TS_Init(LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
	static int16_t last_x = 0;
	static int16_t last_y = 0;

	BSP_TS_GetState(&TS_State);
	if(TS_State.touchDetected != 0) {
		data->point.x = TS_State.touchX[0];
		data->point.y = TS_State.touchY[0];
		last_x = data->point.x;
		last_y = data->point.y;
		data->state = LV_INDEV_STATE_PRESSED;
	} else {
		data->point.x = last_x;
		data->point.y = last_y;
		data->state = LV_INDEV_STATE_RELEASED;
	}
}