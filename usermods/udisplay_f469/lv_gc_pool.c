/*
 * Garbage collection for LVGL's memory pool in SDRAM (lv_conf.h).
 *
 * LVGL objects hold the only references to some Python objects: the Python
 * wrapper of a widget, which keeps its event callbacks, and the callback
 * dicts of timers and other structs. The pool is outside the MicroPython
 * heap, so the garbage collector must trace its used blocks as roots, or it
 * frees objects that are still in use.
 *
 * The port's gc_collect() has no hook for extra roots. The linker therefore
 * redirects its call of gc_collect_end() to __wrap_gc_collect_end()
 * (-Wl,--wrap=gc_collect_end in micropython.mk). At that point all other
 * roots are traced, so tracing the pool and then calling the real
 * gc_collect_end() adds the pool as one more root set. gc_sweep_all() calls
 * gc_collect_end() from inside py/gc.c, where the linker cannot redirect
 * it; that is intended, since it frees everything at soft reset.
 *
 * Soft reset is not supported: LVGL keeps its state in root pointers
 * (mp_lv_roots) that a soft reset does not clear, so afterwards they point
 * into the reinitialized heap, both here and in the next lv_init().
 */
#include "py/gc.h"
#include "lvgl.h"
#include "lvgl/src/core/lv_global.h"
#include "lvgl/src/misc/lv_ll.h"
#include "lvgl/src/stdlib/builtin/lv_tlsf.h"

void __real_gc_collect_end(void);

static void trace_used_block(void *ptr, size_t size, int used, void *user) {
    (void)user;
    if (used) {
        gc_collect_root((void **)ptr, size / sizeof(void *));
    }
}

void __wrap_gc_collect_end(void) {
    if (lv_is_initialized()) {
        lv_pool_t *pool;
        LV_LL_READ(&LV_GLOBAL_DEFAULT()->tlsf_state.pool_ll, pool) {
            lv_tlsf_walk_pool(*pool, trace_used_block, NULL);
        }
    }
    __real_gc_collect_end();
}
