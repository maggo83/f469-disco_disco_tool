#ifndef __LV_STM_HAL_H__
#define __LV_STM_HAL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tft_init();
void touchpad_init();

#if UDISPLAY_F469_ASYNC_DMA2D || UDISPLAY_F469_COMPOSITOR
void tft_poll(void);
#endif

#if UDISPLAY_F469_COMPOSITOR
uint32_t tft_active_framebuffer(void);
int tft_clear_framebuffers(void);
void tft_arm_compositor_irqs(void);
/* Animation types used by the hardware compositor. */
#define TFT_ANIM_HORIZONTAL_SLIDE_IN   1
#define TFT_ANIM_HORIZONTAL_SLIDE_OUT  2
#define TFT_ANIM_HORIZONTAL_PUSH_IN    3
#define TFT_ANIM_HORIZONTAL_PUSH_OUT   4
#define TFT_ANIM_VERTICAL_SLIDE_IN     5
#define TFT_ANIM_VERTICAL_SLIDE_OUT    6

/* Easing curves. Each maps the elapsed time t in 0..1024 (1024 = end) to
 * a position in 0..1024. To add one, append its id here and handle it in
 * transition_math.c. */
#define TFT_EASING_LINEAR              0
#define TFT_EASING_EASE_IN_CUBIC       1
#define TFT_EASING_EASE_OUT_CUBIC      2
#define TFT_EASING_EASE_IN_OUT_CUBIC   3
#define TFT_EASING_EASE_OUT_QUINT      4
#define TFT_EASING__COUNT              5

/* Returns 1 after begin until the transition finishes or is aborted. */
int  tft_transition_active(void);

/* Preserve the current live framebuffer and suppress ordinary LVGL flushes.
 * Call this before changing the active LVGL screen. */
int  tft_transition_begin(void);

/* Start a prepared transition from the framebuffer preserved by begin() to
 * the current LVGL screen.
 *   anim_type:    TFT_ANIM_HORIZONTAL_SLIDE_IN .. TFT_ANIM_VERTICAL_SLIDE_OUT
 *   duration_ms:  duration of the animation
 *   rect_x/y/w/h: the animated rectangle in panel pixels, inside
 *                 LV_HOR_RES_MAX x LV_VER_RES_MAX. Outside it, the new
 *                 screen appears at once.
 *   easing:       TFT_EASING_*
 *   on_done:      called from interrupt context when the last frame is
 *                 shown, with on_done_arg; it must be interrupt-safe.
 * begin() must have succeeded before this call. The current screen is
 * invalidated, rendered once offscreen, and then animated without LVGL.
 * LVGL flushes are dropped until the transition ends, so the caller
 * invalidates the screen in on_done.
 * Returns 0, or -1 if no transition was prepared or an argument is invalid. */
int  tft_transition_start(int anim_type, uint32_t duration_ms,
                          uint16_t rect_x, uint16_t rect_y,
                          uint16_t rect_w, uint16_t rect_h,
                          uint8_t easing,
                          void (*on_done)(void *arg), void *on_done_arg);

/* Cancel a prepared transition before start(). The current LVGL screen is
 * invalidated so normal rendering resumes on the next update. */
int  tft_transition_abort(void);
#endif

#ifdef __cplusplus
}
#endif

#endif //__LV_STM_HAL_H__