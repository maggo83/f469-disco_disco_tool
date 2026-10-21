# F469 display module

Enables the display and LVGL on the STM32F469 Discovery board and Unix port.

The F469 module retains its original blocking rendering path unless features
are explicitly enabled at build time.

| Make variable | Default | Effect |
| --- | --- | --- |
| `UDISPLAY_F469_ASYNC_DMA2D` | `0` | Complete LVGL flushes from the DMA2D interrupt instead of waiting for each copied line. |

Leaving this variable unset preserves the previous blocking flush behavior.
