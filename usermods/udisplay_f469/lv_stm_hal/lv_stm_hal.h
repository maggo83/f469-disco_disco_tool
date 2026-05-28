#ifndef __LV_STM_HAL_H__
#define __LV_STM_HAL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tft_init(void);
void touchpad_init(void);

/* Transition compositor (lv_stm_hal.c). */

/* Animation types; the values match MockUI's GUIAnimations. */
#define TFT_ANIM_NONE                  0
#define TFT_ANIM_HORIZONTAL_SLIDE_IN   1
#define TFT_ANIM_HORIZONTAL_SLIDE_OUT  2
#define TFT_ANIM_HORIZONTAL_PUSH_IN    3
#define TFT_ANIM_HORIZONTAL_PUSH_OUT   4
#define TFT_ANIM_VERTICAL_SLIDE_IN     5
#define TFT_ANIM_VERTICAL_SLIDE_OUT    6

/* Easing curves. Each maps the elapsed time t in 0..1024 (1024 = end) to
 * a position in 0..1024. To add one, implement tft_ease_<name>() in
 * lv_stm_hal.c, append its id here and add it to s_easing_table[] at the
 * same index. */
#define TFT_EASING_LINEAR              0
#define TFT_EASING_EASE_IN_CUBIC       1
#define TFT_EASING_EASE_OUT_CUBIC      2
#define TFT_EASING_EASE_IN_OUT_CUBIC   3
#define TFT_EASING_EASE_OUT_QUINT      4
#define TFT_EASING__COUNT              5

/* Returns 1 while a transition is animating. */
int  tft_transition_active(void);

/* Start a transition of a rectangle from the current screen to the new one.
 *   anim_type:    TFT_ANIM_HORIZONTAL_SLIDE_IN .. TFT_ANIM_VERTICAL_SLIDE_OUT
 *   duration_ms:  duration of the animation
 *   rect_x/y/w/h: the animated rectangle in panel pixels, inside
 *                 LV_HOR_RES_MAX x LV_VER_RES_MAX. Outside it, the new
 *                 screen appears at once.
 *   easing:       TFT_EASING_*
 *   on_done:      called from interrupt context when the last frame is
 *                 shown, with on_done_arg; it must be interrupt-safe.
 * The new screen must already be loaded in LVGL; this renders it once
 * offscreen and then animates without LVGL. LVGL flushes are dropped until
 * the transition ends, so the caller invalidates the screen in on_done.
 * Returns 0, or -1 if a transition is running or an argument is invalid. */
int  tft_transition_start(int anim_type, uint32_t duration_ms,
                          uint16_t rect_x, uint16_t rect_y,
                          uint16_t rect_w, uint16_t rect_h,
                          uint8_t easing,
                          void (*on_done)(void *arg), void *on_done_arg);

#ifdef __cplusplus
}
#endif

#endif //__LV_STM_HAL_H__