#ifndef __LV_STM_HAL_H__
#define __LV_STM_HAL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tft_init(void);
void touchpad_init(void);

/* ============= Phase 3 transition compositor (Option C, 3 SDRAM FBs) ==== */

/* Anim types matching scenarios.MockUI.basic.utils.animations.GUIAnimations.
 * Only horizontal_push_in (=3) is implemented in this wave; calling with
 * any other value returns -1. */
#define TFT_ANIM_NONE                  0
#define TFT_ANIM_HORIZONTAL_SLIDE_IN   1
#define TFT_ANIM_HORIZONTAL_SLIDE_OUT  2
#define TFT_ANIM_HORIZONTAL_PUSH_IN    3
#define TFT_ANIM_HORIZONTAL_PUSH_OUT   4
#define TFT_ANIM_VERTICAL_SLIDE_IN     5
#define TFT_ANIM_VERTICAL_SLIDE_OUT    6

/* Returns 1 if a transition is currently animating. */
int  tft_transition_active(void);

/* Kick a full-screen transition.
 *   anim_type:    one of TFT_ANIM_* above
 *   duration_ms:  total animation duration
 *   on_done_cb:   opaque pointer passed back to on_done; useful for the
 *                 Python binding to schedule a MicroPython callable.
 *   on_done:      called from ISR context once animation completes; must
 *                 be ISR-safe (typically mp_sched_schedule a Python callable).
 * The caller must:
 *   1. Have the NEW LVGL screen already active (lv.scr_load done).
 *   2. Call this function; it captures the current live FB as OLD, then
 *      uses lv_refr_now() with a flush override to render NEW into FB2,
/* Kick a transition that animates a specific rectangular region of the
 * panel. Pixels OUTSIDE the rect are NEVER touched by the compositor;
 * they remain the OLD content on every frame and after the swap. This
 * lets the caller exclude a stationary navigation bar from a sliding
 * content area.
 *   anim_type:    one of TFT_ANIM_* above
 *   duration_ms:  total animation duration
 *   rect_x/y/w/h: animation rectangle in panel pixel coordinates
 *                 (0 <= x, x+w <= LV_HOR_RES_MAX, 0 <= y, y+h <= LV_VER_RES_MAX).
 *                 Pass (0, 0, LV_HOR_RES_MAX, LV_VER_RES_MAX) for a full
 *                 screen animation.
 *   on_done_arg:  opaque pointer passed back to on_done; useful for the
 *                 Python binding to schedule a MicroPython callable.
 *   on_done:      called from ISR context once animation completes; must
 *                 be ISR-safe (typically mp_sched_schedule a Python callable).
 * The caller must:
 *   1. Have the NEW LVGL screen already active (lv.scr_load done).
 *   2. Call this function; it captures the current live FB as OLD, then
 *      uses lv_refr_now() with a flush override to render NEW into FB2,
 *      then starts the ISR-driven compositor.
 * Returns 0 on success, -1 on error (anim already running, or unsupported
 * type or invalid rect). */
int  tft_transition_start(int anim_type, uint32_t duration_ms,
                          uint16_t rect_x, uint16_t rect_y,
                          uint16_t rect_w, uint16_t rect_h,
                          void (*on_done)(void *arg), void *on_done_arg);

#ifdef __cplusplus
}
#endif

#endif //__LV_STM_HAL_H__