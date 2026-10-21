#include "transition_math.h"

int tft_transition_geometry(uint8_t anim_type,
                            uint32_t rx, uint32_t ry,
                            uint32_t rw, uint32_t rh,
                            uint32_t d, uint32_t d_prev,
                            tft_transition_geometry_t *geometry) {
    if (!geometry) return -1;

    tft_transition_slice_t *old = &geometry->old_slice;
    tft_transition_slice_t *new = &geometry->new_slice;
    *old = (tft_transition_slice_t){0};
    *new = (tft_transition_slice_t){0};

    switch (anim_type) {
    case TFT_ANIM_HORIZONTAL_PUSH_IN: {
        uint32_t delta = (d >= d_prev) ? (d - d_prev) : 0u;
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        *old = (tft_transition_slice_t){
            rx + delta, ry, rx, ry, old_w, rh
        };
        *new = (tft_transition_slice_t){
            rx, ry, rx + old_w, ry, d, rh
        };
        return 0;
    }
    case TFT_ANIM_HORIZONTAL_PUSH_OUT: {
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        *old = (tft_transition_slice_t){
            rx + d_prev, ry, rx + d, ry, old_w, rh
        };
        *new = (tft_transition_slice_t){
            rx + old_w, ry, rx, ry, d, rh
        };
        return 0;
    }
    case TFT_ANIM_HORIZONTAL_SLIDE_IN: {
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        *old = (tft_transition_slice_t){
            rx, ry, rx, ry, old_w, rh
        };
        *new = (tft_transition_slice_t){
            rx, ry, rx + old_w, ry, d, rh
        };
        return 0;
    }
    case TFT_ANIM_HORIZONTAL_SLIDE_OUT: {
        uint32_t old_w = (d <= rw) ? (rw - d) : 0u;
        *old = (tft_transition_slice_t){
            rx + d_prev, ry, rx + d, ry, old_w, rh
        };
        *new = (tft_transition_slice_t){
            rx, ry, rx, ry, d, rh
        };
        return 0;
    }
    case TFT_ANIM_VERTICAL_SLIDE_IN: {
        uint32_t old_h = (d <= rh) ? (rh - d) : 0u;
        *old = (tft_transition_slice_t){
            rx, ry, rx, ry, rw, old_h
        };
        *new = (tft_transition_slice_t){
            rx, ry, rx, ry + old_h, rw, d
        };
        return 0;
    }
    case TFT_ANIM_VERTICAL_SLIDE_OUT: {
        uint32_t old_h = (d <= rh) ? (rh - d) : 0u;
        *old = (tft_transition_slice_t){
            rx, ry + d_prev, rx, ry + d, rw, old_h
        };
        *new = (tft_transition_slice_t){
            rx, ry, rx, ry, rw, d
        };
        return 0;
    }
    default:
        return -1;
    }
}

static uint32_t ease_in_cubic(uint32_t t_q) {
    uint32_t t2 = (t_q * t_q) >> 10;
    return (t2 * t_q) >> 10;
}

static uint32_t ease_out_cubic(uint32_t t_q) {
    uint32_t u = 1024u - t_q;
    uint32_t u2 = (u * u) >> 10;
    uint32_t u3 = (u2 * u) >> 10;
    return 1024u - u3;
}

static uint32_t ease_in_out_cubic(uint32_t t_q) {
    if (t_q < 512u) {
        uint32_t t2 = (t_q * t_q) >> 10;
        uint32_t t3 = (t2 * t_q) >> 10;
        return t3 * 4u;
    }

    uint32_t u = (1024u - t_q) * 2u;
    uint32_t u2 = (u * u) >> 10;
    uint32_t u3 = (u2 * u) >> 10;
    return 1024u - (u3 >> 1);
}

static uint32_t ease_out_quint(uint32_t t_q) {
    uint32_t u = 1024u - t_q;
    uint32_t u2 = (u * u) >> 10;
    uint32_t u4 = (u2 * u2) >> 10;
    uint32_t u5 = (u4 * u) >> 10;
    return 1024u - u5;
}

uint32_t tft_transition_ease(uint32_t t_q, uint8_t easing) {
    if (t_q > 1024u) t_q = 1024u;

    switch (easing) {
    case TFT_EASING_LINEAR:
        return t_q;
    case TFT_EASING_EASE_IN_CUBIC:
        return ease_in_cubic(t_q);
    case TFT_EASING_EASE_OUT_CUBIC:
        return ease_out_cubic(t_q);
    case TFT_EASING_EASE_IN_OUT_CUBIC:
        return ease_in_out_cubic(t_q);
    case TFT_EASING_EASE_OUT_QUINT:
        return ease_out_quint(t_q);
    default:
        return t_q;
    }
}
