# F469 display module

Enables the display and LVGL on the STM32F469 Discovery board and Unix port.

The F469 module retains its original behavior unless features are explicitly
enabled at build time.

| Make variable | Default | Effect |
| --- | --- | --- |
| `UDISPLAY_LVGL_POOL_IN_SDRAM` | `0` | Allocate LVGL objects from the configured SDRAM pool and trace it during MicroPython garbage collection. |
| `F469_SDRAM_LAYOUT` | `0` | Use a consumer-supplied `sdram_layout.h` for framebuffer, LVGL pool, and RAM-disk regions. Its directory must be on the compiler include path. |

Leaving these variables unset preserves the previous framebuffer addresses,
LVGL allocation behavior, and SDRAM API.
