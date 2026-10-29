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
#include "transition_math.h"

uint32_t tft_active_framebuffer(void);
int tft_clear_framebuffers(void);
void tft_arm_compositor_irqs(void);

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
 * LVGL invalidation is disabled after the target render until the transition
 * ends, so the caller invalidates the screen in on_done.
 * Returns 0, or -1 if no transition was prepared or an argument is invalid. */
int  tft_transition_start(int anim_type, uint32_t duration_ms,
                          uint16_t rect_x, uint16_t rect_y,
                          uint16_t rect_w, uint16_t rect_h,
                          uint8_t easing,
                          void (*on_done)(void *arg), void *on_done_arg);

/* Cancel a prepared transition before start(). If the target screen was
 * already rendered, publish it before normal rendering resumes. */
int  tft_transition_abort(void);
#endif

#ifdef __cplusplus
}
#endif

#endif //__LV_STM_HAL_H__