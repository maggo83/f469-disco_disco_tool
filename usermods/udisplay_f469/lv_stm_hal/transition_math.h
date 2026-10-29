#ifndef __TRANSITION_MATH_H__
#define __TRANSITION_MATH_H__

#include <stdint.h>

#define TFT_ANIM_HORIZONTAL_SLIDE_IN   1
#define TFT_ANIM_HORIZONTAL_SLIDE_OUT  2
#define TFT_ANIM_HORIZONTAL_PUSH_IN    3
#define TFT_ANIM_HORIZONTAL_PUSH_OUT   4
#define TFT_ANIM_VERTICAL_SLIDE_IN     5
#define TFT_ANIM_VERTICAL_SLIDE_OUT    6

#define TFT_EASING_LINEAR              0
#define TFT_EASING_EASE_IN_CUBIC       1
#define TFT_EASING_EASE_OUT_CUBIC      2
#define TFT_EASING_EASE_IN_OUT_CUBIC   3
#define TFT_EASING_EASE_OUT_QUINT      4
#define TFT_EASING__COUNT              5

typedef struct {
    uint32_t sx;
    uint32_t sy;
    uint32_t dx;
    uint32_t dy;
    uint32_t w;
    uint32_t h;
} tft_transition_slice_t;

typedef struct {
    tft_transition_slice_t old_slice;
    tft_transition_slice_t new_slice;
} tft_transition_geometry_t;

int tft_transition_geometry(uint8_t anim_type,
                            uint32_t rect_x, uint32_t rect_y,
                            uint32_t rect_w, uint32_t rect_h,
                            uint32_t distance, uint32_t previous_distance,
                            tft_transition_geometry_t *geometry);

uint32_t tft_transition_ease(uint32_t time_q, uint8_t easing);

#endif
