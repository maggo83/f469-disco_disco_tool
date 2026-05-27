#include "lv_stm_hal.h"
#include "lv_conf.h"
#include "lvgl/src/display/lv_display.h"
#include "lvgl/src/indev/lv_indev.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_ts.h"
#include "stm32f4xx_hal.h"

static void tft_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/* Display whose flush is currently in-flight on DMA2D. Set by tft_flush()
 * before kicking the transfer, consumed by the DMA2D IRQ callback. */
static volatile lv_display_t *s_disp_in_flight = NULL;

static void tft_dma2d_xfer_cplt_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    lv_display_t *disp = (lv_display_t *)s_disp_in_flight;
    if (disp) {
        s_disp_in_flight = NULL;
        /* LVGL v9 supports calling this from ISR when LV_USE_OS == 0. */
        lv_display_flush_ready(disp);
    }
}

static void tft_dma2d_xfer_err_cb(DMA2D_HandleTypeDef *hdma2d) {
    (void)hdma2d;
    /* On error: still release LVGL so it does not deadlock. */
    lv_display_t *disp = (lv_display_t *)s_disp_in_flight;
    if (disp) {
        s_disp_in_flight = NULL;
        lv_display_flush_ready(disp);
    }
}

/* Override the weak DMA2D_IRQHandler from the STM32F4 startup file so HAL gets
 * the interrupt and dispatches our XferCpltCallback. */
void DMA2D_IRQHandler(void) {
    HAL_DMA2D_IRQHandler(&hdma2d_eval);
}

void tft_init(void) {
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LTDC_ACTIVE_LAYER_BACKGROUND, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_BACKGROUND);
    BSP_LCD_Clear(0xFFFFFFFF);
    BSP_LCD_SetBackColor(0xFFFFFFFF);

    /* Register DMA2D completion callbacks so the ISR releases LVGL. */
    hdma2d_eval.XferCpltCallback  = tft_dma2d_xfer_cplt_cb;
    hdma2d_eval.XferErrorCallback = tft_dma2d_xfer_err_cb;

	// FIXME: try two full-screen buffers in SRAM
	static lv_color_t buf1[LV_HOR_RES_MAX * 30];
    lv_display_t *disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, tft_flush);
    lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

static void tft_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {

#if LV_COLOR_DEPTH == 32
    uint8_t result = LCD_ERROR;

    if(area->x2 >= area->x1 && area->y2 >= area->y1 && px_map) {
        /* Mark the in-flight display BEFORE starting the transfer so that an
         * already-pending IRQ (very short transfers) is handled correctly. */
        s_disp_in_flight = disp;
        result = BSP_LCD_DrawBitmapRaw_IT( area->x1, area->y1,
                                           area->x2 - area->x1 + 1,
                                           area->y2 - area->y1 + 1,
                                           LV_COLOR_DEPTH, px_map );
        if (result != LCD_OK) {
            /* Failed to start: clear marker and report ready synchronously so
             * LVGL does not stall. */
            s_disp_in_flight = NULL;
            lv_display_flush_ready(disp);
            return;
        }
        /* Completion will be signalled from the DMA2D IRQ callback. */
        return;
    }
#else
#   error "Unsupported LV_COLOR_DEPTH"
#endif

    /* Nothing to flush (degenerate area): release immediately. */
    lv_display_flush_ready(disp);
}

/**************** touchpad ****************/

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
static TS_StateTypeDef  TS_State;

void touchpad_init(void) {
    BSP_TS_Init(LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
	static int16_t last_x = 0;
	static int16_t last_y = 0;

	BSP_TS_GetState(&TS_State);
	if(TS_State.touchDetected != 0) {
		data->point.x = TS_State.touchX[0];
		data->point.y = TS_State.touchY[0];
		last_x = data->point.x;
		last_y = data->point.y;
		data->state = LV_INDEV_STATE_PRESSED;
	} else {
		data->point.x = last_x;
		data->point.y = last_y;
		data->state = LV_INDEV_STATE_RELEASED;
	}
}