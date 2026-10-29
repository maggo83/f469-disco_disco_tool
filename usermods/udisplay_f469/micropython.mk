DISPLAY_MOD_DIR := $(USERMOD_DIR)
UDISPLAY_LV_CONF_OVERRIDE ?=
UDISPLAY_LV_MPY ?=

ifneq ($(UDISPLAY_LV_CONF_OVERRIDE)$(UDISPLAY_LV_MPY),)
ifeq ($(UDISPLAY_LV_CONF_OVERRIDE),)
$(error UDISPLAY_LV_MPY requires a matching UDISPLAY_LV_CONF_OVERRIDE)
endif
ifeq ($(UDISPLAY_LV_MPY),)
$(error UDISPLAY_LV_CONF_OVERRIDE requires a matching UDISPLAY_LV_MPY)
endif
endif

ifneq ($(UDISPLAY_LV_CONF_OVERRIDE),)
CFLAGS_USERMOD += -DUDISPLAY_LV_CONF_OVERRIDE='"$(UDISPLAY_LV_CONF_OVERRIDE)"'
endif
ifneq ($(UDISPLAY_LV_MPY),)
CFLAGS_USERMOD += -DUDISPLAY_LV_MPY='"$(UDISPLAY_LV_MPY)"'
endif

# stm32f469
ifeq ($(CMSIS_MCU),STM32F469xx)
UDISPLAY_F469_COMPOSITOR ?= 0
UDISPLAY_F469_ASYNC_DMA2D ?= 0
UDISPLAY_F469_RGB565 ?= 0
UDISPLAY_LVGL_POOL_IN_SDRAM ?= 0
F469_SDRAM_LAYOUT ?= 0

CFLAGS_USERMOD += -DUDISPLAY_F469_COMPOSITOR=$(UDISPLAY_F469_COMPOSITOR)
CFLAGS_USERMOD += -DUDISPLAY_F469_ASYNC_DMA2D=$(UDISPLAY_F469_ASYNC_DMA2D)
CFLAGS_USERMOD += -DUDISPLAY_F469_RGB565=$(UDISPLAY_F469_RGB565)
CFLAGS_USERMOD += -DUDISPLAY_LVGL_POOL_IN_SDRAM=$(UDISPLAY_LVGL_POOL_IN_SDRAM)
CFLAGS_USERMOD += -DF469_SDRAM_LAYOUT=$(F469_SDRAM_LAYOUT)

ifeq ($(UDISPLAY_F469_COMPOSITOR),1)
ifneq ($(UDISPLAY_F469_ASYNC_DMA2D),1)
$(error UDISPLAY_F469_COMPOSITOR=1 requires UDISPLAY_F469_ASYNC_DMA2D=1)
endif
ifneq ($(UDISPLAY_F469_RGB565),1)
$(error UDISPLAY_F469_COMPOSITOR=1 requires UDISPLAY_F469_RGB565=1)
endif
endif
ifeq ($(UDISPLAY_F469_RGB565),1)
ifneq ($(UDISPLAY_F469_COMPOSITOR),1)
$(error UDISPLAY_F469_RGB565=1 requires UDISPLAY_F469_COMPOSITOR=1)
endif
endif

ifneq ($(filter 1,$(UDISPLAY_F469_COMPOSITOR) $(UDISPLAY_LVGL_POOL_IN_SDRAM)),)
ifneq ($(F469_SDRAM_LAYOUT),1)
$(error F469_SDRAM_LAYOUT=1 is required by the compositor and SDRAM LVGL pool)
endif
endif

ifeq ($(UDISPLAY_F469_COMPOSITOR),1)
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_stm_hal/lv_stm_hal_compositor.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_stm_hal/transition_math.c
else ifeq ($(UDISPLAY_F469_ASYNC_DMA2D),1)
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_stm_hal/lv_stm_hal_async.c
else
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_stm_hal/lv_stm_hal.c
endif

# LVGL allocates from a pool in SDRAM; lv_gc_pool.c makes the garbage
# collector trace it by taking over the port's call of gc_collect_end().
ifeq ($(UDISPLAY_LVGL_POOL_IN_SDRAM),1)
LDFLAGS_USERMOD += -Wl,--wrap=gc_collect_end
ifeq ($(LTO),1)
$(error LTO can bypass -Wl,--wrap=gc_collect_end, and the garbage collector would then free objects LVGL still references)
endif
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_gc_pool.c
endif
# The module itself
SRC_USERMOD += $(DISPLAY_MOD_DIR)/display.c

# LCD display support
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Utilities/Fonts/font12.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Utilities/Fonts/font16.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Utilities/Fonts/font20.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Utilities/Fonts/font24.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Utilities/Fonts/font8.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/STM32469I-Discovery/stm32469i_discovery.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/STM32469I-Discovery/stm32469i_discovery_lcd.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/STM32469I-Discovery/stm32469i_discovery_sdram.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/Components/otm8009a/otm8009a.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/Components/nt35510/nt35510.c

# Touchscreen support
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/STM32469I-Discovery/stm32469i_discovery_ts.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/Components/ft6x06/ft6x06.c

# FIXME: this should be included automatically
# as these files are in micropython repo as well
# Probably changes in mpboardconfigport.mk or some #defines can help
SRC_USERMOD += $(DISPLAY_MOD_DIR)/STM32F4xx_HAL_Driver/stm32f4xx_hal_dsi.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/STM32F4xx_HAL_Driver/stm32f4xx_hal_ltdc.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/STM32F4xx_HAL_Driver/stm32f4xx_hal_ltdc_ex.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/STM32F4xx_HAL_Driver/stm32f4xx_hal_dma2d.c

# lvgl support
LVGL_DIR := $(DISPLAY_MOD_DIR)
LVGL_PATH := $(LVGL_DIR)/lvgl
include $(LVGL_DIR)/lvgl/lvgl.mk
SRC_USERMOD += $(CSRCS)
CFLAGS_USERMOD += $(CFLAGS)

# square font
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/square.c
# roboto mono font
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_28.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_22.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_16.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_12.c

# Dirs with header files
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/lvgl
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/lv_stm_hal
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/BSP_DISCO_F469NI/Drivers/BSP/STM32469I-Discovery

endif

ifneq ($(UNAME_S),)
# unixport (mac / linux)

# The module itself
SRC_USERMOD += $(DISPLAY_MOD_DIR)/display_unix.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/display_unix_sdl.c

# lvgl support
LVGL_DIR := $(DISPLAY_MOD_DIR)
LVGL_PATH := $(LVGL_DIR)/lvgl
include $(LVGL_DIR)/lvgl/lvgl.mk
SRC_USERMOD += $(CSRCS)
CFLAGS_USERMOD += $(CFLAGS) -DMICROPY_SDL=1

# square font
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/square.c
# roboto mono font
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_28.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_22.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_16.c
SRC_USERMOD += $(DISPLAY_MOD_DIR)/fonts/font_roboto_mono_12.c

# Dirs with header files
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/lvgl
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/lv_sdl_hal/SDL

LDFLAGS_MOD += -lSDL2

endif

QSTR_DEFS += $(DISPLAY_MOD_DIR)/udisplay.qstr
