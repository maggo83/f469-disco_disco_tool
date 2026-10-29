# F469 display module

Enables the display and LVGL on the STM32F469 Discovery board and Unix port.

The F469 module retains its original blocking ARGB8888 rendering path unless
features are explicitly enabled at build time.

| Make variable | Default | Effect |
| --- | --- | --- |
| `UDISPLAY_F469_ASYNC_DMA2D` | `0` | Complete LVGL flushes from the DMA2D interrupt instead of waiting for each copied line. |
| `UDISPLAY_F469_COMPOSITOR` | `0` | Enable direct RGB565 double buffering for ordinary refreshes, the triple-framebuffer transition compositor, and its Python API. Requires asynchronous DMA2D, RGB565, and the configured SDRAM layout. |
| `UDISPLAY_F469_RGB565` | `0` | Use RGB565 for the LTDC layer and all compositor framebuffers. Required by the compositor path. |
| `UDISPLAY_LVGL_POOL_IN_SDRAM` | `0` | Allocate LVGL objects from the configured SDRAM pool and trace it during MicroPython garbage collection. |
| `F469_SDRAM_LAYOUT` | `0` | Use a consumer-supplied `sdram_layout.h` for framebuffer, LVGL pool, and RAM-disk regions. Its directory must be on the compiler include path. |

A compositor build uses:

```console
make disco \
    F469_SDRAM_LAYOUT=1 \
    UDISPLAY_F469_ASYNC_DMA2D=1 \
    UDISPLAY_F469_COMPOSITOR=1 \
    UDISPLAY_F469_RGB565=1 \
    UDISPLAY_LVGL_POOL_IN_SDRAM=1
```

Leaving these variables unset preserves the previous framebuffer count, draw
format, blocking flush behavior, touch handling, SDRAM API, and Python display
API.

In a compositor build, LVGL renders directly into one of two full RGB565
framebuffers in SDRAM. The display coordinator publishes the completed buffer
only at vertical blanking. LVGL synchronizes unchanged areas between this
normal pair before drawing the next invalidated regions.

Screen transitions use the same coordinator and all three framebuffers. LVGL
renders the target screen once into the normal offscreen buffer. DMA2D then
copies transition rectangles between the preserved old screen, the pinned
target screen, and the third framebuffer. No LVGL rendering occurs while the
animation is active. The final vertical-blanked switch publishes the pinned
target buffer and restores normal direct rendering.
