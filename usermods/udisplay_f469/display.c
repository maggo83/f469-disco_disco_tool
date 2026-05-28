// Include required definitions first.
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "lvgl.h"
#include "lv_stm_hal.h"
#include "stm32469i_discovery_lcd.h"

#if MODULE_DISPLAY_ENABLED

/* Python callable to invoke when the Phase 3 transition compositor
 * finishes. The Python caller is responsible for keeping a strong
 * reference to the callable until it is invoked (typically stored on
 * the SpecterGui instance). */
STATIC mp_obj_t s_transition_done_cb = mp_const_none;

STATIC mp_obj_t display_init(){
    lv_init();
    tft_init();
    touchpad_init();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_init_obj, display_init);

STATIC mp_obj_t display_update(mp_obj_t dt_obj){
    uint32_t dt = mp_obj_get_int(dt_obj);
    lv_tick_inc(dt);
    lv_task_handler();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_update_obj, display_update);

STATIC mp_obj_t display_on(){
    BSP_LCD_DisplayOn();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_on_obj, display_on);

STATIC mp_obj_t display_off(){
    BSP_LCD_DisplayOff();
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_off_obj, display_off);

STATIC mp_obj_t display_set_rotation(mp_obj_t rot_obj){
    int rot_int = mp_obj_get_int(rot_obj);
    if(rot_int < 0 || rot_int > 1){
        mp_raise_ValueError(MP_ERROR_TEXT("Rotation can be 0 or 1"));
        return mp_const_none;
    }
    LCD_OrientationTypeDef rot = LCD_ORIENTATION_PORTRAIT;
    if(rot_int == 1){
        rot = LCD_ORIENTATION_LANDSCAPE;
    }
    BSP_LCD_InitEx(rot, 0);
    BSP_LCD_LayerDefaultInit(LTDC_ACTIVE_LAYER_BACKGROUND, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_BACKGROUND);
    BSP_LCD_Clear(0xFFFFFFFF);
    BSP_LCD_SetBackColor(0xFFFFFFFF);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_set_rotation_obj, display_set_rotation);

/* Trampoline invoked from the LTDC reload IRQ when the compositor
 * finishes. Defers the actual Python call to the scheduler so we never
 * run Python code inside an ISR context. */
STATIC void transition_done_trampoline(void *arg) {
    (void)arg;
    mp_obj_t cb = s_transition_done_cb;
    if (cb != mp_const_none) {
        s_transition_done_cb = mp_const_none;
        mp_sched_schedule(cb, mp_const_none);
    }
}

STATIC mp_obj_t display_transition(size_t n_args, const mp_obj_t *args) {
    /* Signature: udisplay.transition(type, dur_ms, x, y, w, h, [cb]) */
    int anim_type = mp_obj_get_int(args[0]);
    uint32_t dur_ms = (uint32_t)mp_obj_get_int(args[1]);
    int rx = mp_obj_get_int(args[2]);
    int ry = mp_obj_get_int(args[3]);
    int rw = mp_obj_get_int(args[4]);
    int rh = mp_obj_get_int(args[5]);
    mp_obj_t cb = (n_args > 6) ? args[6] : mp_const_none;
    if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 ||
        rx > 0xFFFF || ry > 0xFFFF || rw > 0xFFFF || rh > 0xFFFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid rect"));
    }
    if (tft_transition_active()) {
        mp_raise_OSError(MP_EBUSY);
    }
    s_transition_done_cb = cb;
    int rc = tft_transition_start(anim_type, dur_ms,
                                  (uint16_t)rx, (uint16_t)ry,
                                  (uint16_t)rw, (uint16_t)rh,
                                  transition_done_trampoline, NULL);
    if (rc != 0) {
        s_transition_done_cb = mp_const_none;
        mp_raise_ValueError(MP_ERROR_TEXT("transition rejected"));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(display_transition_obj, 6, 7, display_transition);

STATIC mp_obj_t display_transition_active(void) {
    return mp_obj_new_bool(tft_transition_active());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_transition_active_obj, display_transition_active);

/****************************** MODULE ******************************/

/* NOTE: the module globals table and module object are defined AFTER
 * '#include "lv_mpy.c"' on purpose. micropython's makeqstrdefs.py has a
 * quirk where it writes the per-file qstr output in "w" mode each time
 * the C preprocessor exits and re-enters a translation unit. Because
 * display.c includes lv_mpy.c (a huge generated TU) before its module
 * registrations, anything *above* the include lands in an earlier
 * write_out that is then OVERWRITTEN by the final chunk of display.c.
 * Keeping every new MP_QSTR_xxx usage in the final chunk (i.e. below
 * the include) ensures the qstr extractor records it. */

#include "lv_mpy.c"

STATIC const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&display_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&display_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&display_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_rotation), MP_ROM_PTR(&display_set_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_transition), MP_ROM_PTR(&display_transition_obj) },
    { MP_ROM_QSTR(MP_QSTR_transition_active), MP_ROM_PTR(&display_transition_active_obj) },
};
STATIC MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

// Define module object.
const mp_obj_module_t display_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&display_module_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR_udisplay, display_user_cmodule);
MP_REGISTER_MODULE(MP_QSTR_lvgl, mp_module_lvgl);

#endif // MODULE_DISPLAY_ENABLED