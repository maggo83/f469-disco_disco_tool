#include "lv_stm_hal.h"
#include "transition_math.h"
#include "lv_conf.h"
#include "lvgl/src/display/lv_display.h"
#include "lvgl/src/indev/lv_indev.h"
#include "lvgl/src/core/lv_refr.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_ts.h"
#include "sdram_layout.h"
#include "stm32f4xx_hal.h"

#if !UDISPLAY_F469_RGB565
#error "the F469 compositor requires RGB565 framebuffers"
#endif

#if SDRAM_FB_BYTES < (LV_HOR_RES_MAX * LV_VER_RES_MAX * 2u)
#error "configured framebuffers are too small for RGB565"
#endif

#define MODE_NORMAL    0
#define MODE_PREPARED  1
#define MODE_ANIMATING 2

#define PRESENT_NONE         0
#define PRESENT_NORMAL       1
#define PRESENT_ANIMATION    2
#define PRESENT_FINAL_TARGET 3

#define DMA_IDLE    0
#define DMA_COMPOSE 1

#define CSTEP_SEED    0
#define CSTEP_OLD     1
#define CSTEP_NEW     2
#define CSTEP_DONE    3

#define WAIT_TIMEOUT_MS 100u

typedef struct {
    volatile uint8_t mode;
    volatile uint8_t present_kind;
    volatile uint8_t armed_kind;
    volatile uint8_t dma_state;
    volatile uint8_t compose_step;
    volatile uint8_t final_requested;
    volatile uint8_t seeded_buffers;
    volatile uint32_t front_fb;
    volatile uint32_t present_fb;
    volatile uint32_t armed_fb;
    volatile uint32_t present_started_ms;
    volatile uint32_t dma_started_ms;
    volatile lv_display_t *disp_in_flight;

    volatile uint8_t type;
    volatile uint8_t easing;
    volatile uint32_t start_ms;
    volatile uint32_t duration_ms;
    volatile uint32_t old_fb;
    volatile uint32_t new_fb;
    volatile uint32_t scan_fb;
    volatile uint32_t next_fb;
    volatile uint32_t previous_distance;
    volatile uint32_t distance;
    volatile uint16_t rect_x;
    volatile uint16_t rect_y;
    volatile uint16_t rect_w;
    volatile uint16_t rect_h;
    void (*on_done)(void *);
    void *on_done_arg;
} display_state_t;

static display_state_t s_state = {
    .mode = MODE_NORMAL,
    .front_fb = SDRAM_FB0_ADDR,
    .compose_step = CSTEP_DONE,
    .easing = TFT_EASING_LINEAR,
};
static lv_display_t *s_display;

static void tft_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map);
static void tft_flush_wait(lv_display_t *disp);
static void compose_kick_tick(uint32_t now_ms);
static void compose_run(uint8_t starting_step);
static void request_final_target(void);

static uint8_t framebuffer_bit(uint32_t framebuffer) {
    if (framebuffer == SDRAM_FB0_ADDR) return 1u;
    if (framebuffer == SDRAM_FB1_ADDR) return 2u;
    if (framebuffer == SDRAM_FB2_ADDR) return 4u;
    return 0u;
}

static uint32_t remaining_framebuffer(uint32_t first, uint32_t second) {
    static const uint32_t framebuffers[] = {
        SDRAM_FB0_ADDR,
        SDRAM_FB1_ADDR,
        SDRAM_FB2_ADDR,
    };
    for (size_t i = 0; i < 3; i++) {
        if (framebuffers[i] != first && framebuffers[i] != second) {
            return framebuffers[i];
        }
    }
    return 0;
}

static void queue_presentation(uint8_t kind, uint32_t framebuffer) {
    s_state.present_kind = PRESENT_NONE;
    s_state.present_fb = framebuffer;
    s_state.present_started_ms = HAL_GetTick();
    s_state.present_kind = kind;
}

static void finish_transition(void) {
    void (*on_done)(void *) = s_state.on_done;
    void *on_done_arg = s_state.on_done_arg;

    s_state.mode = MODE_NORMAL;
    s_state.final_requested = 0;
    s_state.compose_step = CSTEP_DONE;
    s_state.old_fb = 0;
    s_state.new_fb = 0;
    s_state.scan_fb = 0;
    s_state.next_fb = 0;
    s_state.on_done = NULL;
    s_state.on_done_arg = NULL;
    lv_display_enable_invalidation(s_display, true);

    if (on_done) on_done(on_done_arg);
}

static void commit_presentation(uint8_t kind, uint32_t framebuffer) {
    s_state.front_fb = framebuffer;
    if (s_state.present_kind == kind &&
        s_state.present_fb == framebuffer) {
        s_state.present_kind = PRESENT_NONE;
        s_state.present_fb = 0;
    }
    s_state.armed_kind = PRESENT_NONE;
    s_state.armed_fb = 0;

    if (kind == PRESENT_NORMAL) {
        lv_display_t *disp = (lv_display_t *)s_state.disp_in_flight;
        s_state.disp_in_flight = NULL;
        if (disp) lv_display_flush_ready(disp);
        return;
    }

    if (kind == PRESENT_FINAL_TARGET) {
        finish_transition();
        return;
    }

    if (kind == PRESENT_ANIMATION && s_state.mode == MODE_ANIMATING) {
        uint32_t previous_scan = s_state.scan_fb;
        s_state.scan_fb = framebuffer;
        s_state.next_fb = previous_scan;
        s_state.previous_distance = s_state.distance;
        compose_kick_tick(HAL_GetTick());
    }
}

static void recover_presentation(void) {
    uint8_t kind = s_state.armed_kind != PRESENT_NONE
                   ? s_state.armed_kind : s_state.present_kind;
    uint32_t framebuffer = s_state.armed_kind != PRESENT_NONE
                           ? s_state.armed_fb : s_state.present_fb;
    if (kind == PRESENT_NONE || framebuffer == 0) return;

    HAL_LTDC_SetAddress_NoReload(
        &hltdc_eval, framebuffer, LTDC_ACTIVE_LAYER_BACKGROUND);
    (void)HAL_LTDC_Reload(&hltdc_eval, LTDC_RELOAD_IMMEDIATE);
    commit_presentation(kind, framebuffer);
}

static int wait_for_presentation(void) {
    uint32_t started = HAL_GetTick();
    while (s_state.present_kind != PRESENT_NONE ||
           s_state.armed_kind != PRESENT_NONE ||
           s_state.disp_in_flight != NULL) {
        if (HAL_GetTick() - started >= WAIT_TIMEOUT_MS) {
            recover_presentation();
            return -1;
        }
    }
    return 0;
}

static void request_final_target(void) {
    if (s_state.mode != MODE_ANIMATING || s_state.final_requested) return;

    s_state.final_requested = 1;
    s_state.dma_state = DMA_IDLE;
    s_state.compose_step = CSTEP_DONE;
    queue_presentation(PRESENT_FINAL_TARGET, s_state.new_fb);
}

static void tft_dma2d_xfer_cplt_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    if (s_state.dma_state != DMA_COMPOSE) return;

    if (s_state.compose_step == CSTEP_SEED) {
        s_state.seeded_buffers |= framebuffer_bit(s_state.next_fb);
    }
    s_state.dma_state = DMA_IDLE;
    compose_run((uint8_t)(s_state.compose_step + 1u));
}

static void tft_dma2d_xfer_err_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    s_state.dma_state = DMA_IDLE;
    request_final_target();
}

void DMA2D_IRQHandler(void) {
    HAL_DMA2D_IRQHandler(&hdma2d_eval);
}

void LTDC_IRQHandler(void) {
    HAL_LTDC_IRQHandler(&hltdc_eval);
}

void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc) {
    if (s_state.present_kind != PRESENT_NONE &&
        s_state.armed_kind == PRESENT_NONE &&
        s_state.dma_state == DMA_IDLE) {
        uint8_t kind = s_state.present_kind;
        uint32_t framebuffer = s_state.present_fb;

        HAL_LTDC_SetAddress_NoReload(
            hltdc, framebuffer, LTDC_ACTIVE_LAYER_BACKGROUND);
        if (HAL_LTDC_Reload(hltdc, LTDC_RELOAD_VERTICAL_BLANKING) == HAL_OK) {
            s_state.armed_kind = kind;
            s_state.armed_fb = framebuffer;
            s_state.present_kind = PRESENT_NONE;
            s_state.present_fb = 0;
        } else if (kind == PRESENT_NORMAL) {
            lv_display_t *disp = (lv_display_t *)s_state.disp_in_flight;
            s_state.disp_in_flight = NULL;
            s_state.present_kind = PRESENT_NONE;
            s_state.present_fb = 0;
            if (disp) lv_display_flush_ready(disp);
        } else {
            request_final_target();
        }
    }
    HAL_LTDC_ProgramLineEvent(hltdc, 0);
}

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc) {
    (void)hltdc;
    uint8_t kind = s_state.armed_kind;
    uint32_t framebuffer = s_state.armed_fb;
    if (kind != PRESENT_NONE && framebuffer != 0) {
        commit_presentation(kind, framebuffer);
    }
}

static void start_compose_copy(uint32_t source, uint32_t source_x,
                               uint32_t source_y, uint32_t destination,
                               uint32_t destination_x,
                               uint32_t destination_y, uint32_t width,
                               uint32_t height) {
    s_state.dma_state = DMA_COMPOSE;
    s_state.dma_started_ms = HAL_GetTick();
    if (BSP_LCD_CopyRectEx_IT(
            source, source_x, source_y,
            destination, destination_x, destination_y,
            width, height) != LCD_OK) {
        s_state.dma_state = DMA_IDLE;
        request_final_target();
    }
}

static void compose_run(uint8_t starting_step) {
    tft_transition_geometry_t geometry;
    if (s_state.mode != MODE_ANIMATING || s_state.final_requested) return;
    if (tft_transition_geometry(
            s_state.type,
            s_state.rect_x, s_state.rect_y,
            s_state.rect_w, s_state.rect_h,
            s_state.distance, s_state.previous_distance,
            &geometry) != 0) {
        request_final_target();
        return;
    }

    uint8_t step = starting_step;
    while (step < CSTEP_DONE) {
        s_state.compose_step = step;
        if (step == CSTEP_SEED) {
            if (!(s_state.seeded_buffers &
                  framebuffer_bit(s_state.next_fb))) {
                start_compose_copy(
                    s_state.new_fb, 0, 0,
                    s_state.next_fb, 0, 0,
                    LV_HOR_RES_MAX, LV_VER_RES_MAX);
                return;
            }
        } else if (step == CSTEP_OLD) {
            tft_transition_slice_t *old = &geometry.old_slice;
            if (old->w > 0 && old->h > 0) {
                start_compose_copy(
                    s_state.scan_fb, old->sx, old->sy,
                    s_state.next_fb, old->dx, old->dy,
                    old->w, old->h);
                return;
            }
        } else if (step == CSTEP_NEW) {
            tft_transition_slice_t *new = &geometry.new_slice;
            if (new->w > 0 && new->h > 0) {
                start_compose_copy(
                    s_state.new_fb, new->sx, new->sy,
                    s_state.next_fb, new->dx, new->dy,
                    new->w, new->h);
                return;
            }
        }
        step++;
    }

    s_state.compose_step = CSTEP_DONE;
    s_state.dma_state = DMA_IDLE;
    queue_presentation(PRESENT_ANIMATION, s_state.next_fb);
}

static void compose_kick_tick(uint32_t now_ms) {
    uint8_t vertical =
        s_state.type == TFT_ANIM_VERTICAL_SLIDE_IN ||
        s_state.type == TFT_ANIM_VERTICAL_SLIDE_OUT;
    uint32_t extent = vertical ? s_state.rect_h : s_state.rect_w;
    uint32_t elapsed = now_ms - s_state.start_ms;

    if (elapsed >= s_state.duration_ms) {
        request_final_target();
        return;
    }

    uint32_t time_q = (elapsed * 1024u) / s_state.duration_ms;
    uint32_t eased_q = tft_transition_ease(time_q, s_state.easing);
    uint32_t distance = (extent * eased_q) / 1024u;
    if (distance < s_state.previous_distance) {
        distance = s_state.previous_distance;
    }
    s_state.distance = distance;
    compose_run(CSTEP_SEED);
}

int tft_transition_active(void) {
    return s_state.mode != MODE_NORMAL;
}

void tft_poll(void) {
    uint32_t now = HAL_GetTick();

    if ((s_state.present_kind != PRESENT_NONE ||
         s_state.armed_kind != PRESENT_NONE) &&
        now - s_state.present_started_ms > WAIT_TIMEOUT_MS) {
        recover_presentation();
    }

    if (s_state.dma_state != DMA_IDLE &&
        now - s_state.dma_started_ms > WAIT_TIMEOUT_MS) {
        (void)HAL_DMA2D_Abort(&hdma2d_eval);
        s_state.dma_state = DMA_IDLE;
        request_final_target();
    }

    if (s_state.mode == MODE_ANIMATING &&
        now - s_state.start_ms > s_state.duration_ms + 500u) {
        request_final_target();
    }
}

int tft_transition_begin(void) {
    if (s_state.mode != MODE_NORMAL) return -1;
    if (wait_for_presentation() != 0) return -1;
    if (s_state.dma_state != DMA_IDLE) return -1;

    s_state.mode = MODE_PREPARED;
    s_state.old_fb = s_state.front_fb;
    s_state.new_fb = 0;
    return 0;
}

int tft_transition_abort(void) {
    if (s_state.mode != MODE_PREPARED) return -1;

    if (s_state.new_fb != 0 && s_state.new_fb != s_state.front_fb) {
        queue_presentation(PRESENT_NORMAL, s_state.new_fb);
        if (wait_for_presentation() != 0 &&
            s_state.front_fb != s_state.new_fb) {
            return -1;
        }
    }
    s_state.mode = MODE_NORMAL;
    s_state.old_fb = 0;
    s_state.new_fb = 0;
    lv_obj_invalidate(lv_screen_active());
    return 0;
}

int tft_transition_start(int anim_type, uint32_t duration_ms,
                         uint16_t rect_x, uint16_t rect_y,
                         uint16_t rect_w, uint16_t rect_h,
                         uint8_t easing,
                         void (*on_done)(void *), void *on_done_arg) {
    if (s_state.mode != MODE_PREPARED) return -1;
    if (anim_type < TFT_ANIM_HORIZONTAL_SLIDE_IN ||
        anim_type > TFT_ANIM_VERTICAL_SLIDE_OUT) return -1;
    if (easing >= TFT_EASING__COUNT) return -1;
    if (duration_ms == 0 || rect_w == 0 || rect_h == 0) return -1;
    if ((uint32_t)rect_x + rect_w > LV_HOR_RES_MAX ||
        (uint32_t)rect_y + rect_h > LV_VER_RES_MAX) return -1;

    s_state.type = (uint8_t)anim_type;
    s_state.easing = easing;
    s_state.duration_ms = duration_ms;
    s_state.rect_x = rect_x;
    s_state.rect_y = rect_y;
    s_state.rect_w = rect_w;
    s_state.rect_h = rect_h;
    s_state.on_done = on_done;
    s_state.on_done_arg = on_done_arg;

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_display);
    if (s_state.new_fb == 0 || s_state.new_fb == s_state.old_fb) {
        s_state.on_done = NULL;
        s_state.on_done_arg = NULL;
        return -1;
    }

    s_state.scan_fb = s_state.old_fb;
    s_state.next_fb =
        remaining_framebuffer(s_state.old_fb, s_state.new_fb);
    if (s_state.next_fb == 0 ||
        BSP_LCD_CopyRectEx(
            s_state.new_fb, 0, 0,
            s_state.next_fb, 0, 0,
            LV_HOR_RES_MAX, LV_VER_RES_MAX) != LCD_OK) {
        s_state.on_done = NULL;
        s_state.on_done_arg = NULL;
        return -1;
    }

    s_state.seeded_buffers = framebuffer_bit(s_state.next_fb);
    s_state.previous_distance = 0;
    s_state.distance = 0;
    s_state.final_requested = 0;
    s_state.start_ms = HAL_GetTick();
    s_state.mode = MODE_ANIMATING;
    lv_display_enable_invalidation(s_display, false);
    compose_kick_tick(s_state.start_ms);
    return 0;
}

void tft_init(void) {
    BSP_LCD_Init();
    BSP_LCD_LayerRgb565Init(
        LTDC_ACTIVE_LAYER_BACKGROUND, SDRAM_FB0_ADDR);
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_BACKGROUND);
    BSP_LCD_SetBackColor(0xFFFFFFFF);

    static const uint32_t framebuffers[] = {
        SDRAM_FB0_ADDR,
        SDRAM_FB1_ADDR,
        SDRAM_FB2_ADDR,
    };
    uint32_t pixel_count = (uint32_t)LV_HOR_RES_MAX * LV_VER_RES_MAX;
    for (size_t buffer = 0; buffer < 3; buffer++) {
        volatile uint16_t *pixels =
            (volatile uint16_t *)framebuffers[buffer];
        for (uint32_t pixel = 0; pixel < pixel_count; pixel++) {
            pixels[pixel] = 0xFFFFu;
        }
    }

    s_state = (display_state_t){
        .mode = MODE_NORMAL,
        .front_fb = SDRAM_FB0_ADDR,
        .compose_step = CSTEP_DONE,
        .easing = TFT_EASING_LINEAR,
    };

    hdma2d_eval.XferCpltCallback = tft_dma2d_xfer_cplt_cb;
    hdma2d_eval.XferErrorCallback = tft_dma2d_xfer_err_cb;

    tft_arm_compositor_irqs();

    s_display = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, tft_flush);
    lv_display_set_flush_wait_cb(s_display, tft_flush_wait);
    lv_display_set_buffers(
        s_display,
        (void *)SDRAM_FB1_ADDR,
        (void *)SDRAM_FB0_ADDR,
        SDRAM_FB_BYTES,
        LV_DISPLAY_RENDER_MODE_DIRECT);
}

void tft_arm_compositor_irqs(void) {
    __HAL_LTDC_ENABLE_IT(&hltdc_eval, LTDC_IT_RR);
    HAL_LTDC_ProgramLineEvent(&hltdc_eval, 0);
}

uint32_t tft_active_framebuffer(void) {
    return s_state.front_fb;
}

int tft_clear_framebuffers(void) {
    if (tft_transition_active() || wait_for_presentation() != 0 ||
        s_state.dma_state != DMA_IDLE) {
        return -1;
    }

    static const uint32_t framebuffers[] = {
        SDRAM_FB0_ADDR,
        SDRAM_FB1_ADDR,
        SDRAM_FB2_ADDR,
    };
    uint32_t pixel_count = (uint32_t)LV_HOR_RES_MAX * LV_VER_RES_MAX;
    for (size_t buffer = 0; buffer < 3; buffer++) {
        volatile uint16_t *pixels =
            (volatile uint16_t *)framebuffers[buffer];
        for (uint32_t pixel = 0; pixel < pixel_count; pixel++) {
            pixels[pixel] = 0;
        }
    }
    lv_obj_invalidate(lv_screen_active());
    return 0;
}

static void tft_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map) {
    if (area->x2 < area->x1 || area->y2 < area->y1 || !px_map) {
        lv_display_flush_ready(disp);
        return;
    }

    if (s_state.mode == MODE_ANIMATING) {
        request_final_target();
        lv_display_flush_ready(disp);
        return;
    }

    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    uint32_t framebuffer = (uint32_t)px_map;
    if (s_state.mode == MODE_PREPARED) {
        s_state.new_fb = framebuffer;
        lv_display_flush_ready(disp);
        return;
    }

    if (framebuffer == s_state.front_fb ||
        s_state.present_kind != PRESENT_NONE ||
        s_state.armed_kind != PRESENT_NONE ||
        s_state.disp_in_flight != NULL) {
        lv_display_flush_ready(disp);
        return;
    }

    s_state.disp_in_flight = disp;
    queue_presentation(PRESENT_NORMAL, framebuffer);
}

static void tft_flush_wait(lv_display_t *disp) {
    while (s_state.disp_in_flight == disp) {
        if (HAL_GetTick() - s_state.present_started_ms >= WAIT_TIMEOUT_MS) {
            recover_presentation();
            return;
        }
    }
}

/**************** touchpad ****************/

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
static TS_StateTypeDef TS_State;

void touchpad_init(void) {
    BSP_TS_Init(LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    (void)indev;

    if (tft_transition_active()) {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    BSP_TS_GetState(&TS_State);
    if (TS_State.touchDetected != 0) {
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
