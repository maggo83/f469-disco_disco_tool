#include "lv_stm_hal.h"
#include "lv_conf.h"
#include "lvgl/src/display/lv_display.h"
#include "lvgl/src/indev/lv_indev.h"
#include "stm32469i_discovery_lcd.h"
#include "stm32469i_discovery_ts.h"
#include "stm32f4xx_hal.h"

#define DMA2D_TIMEOUT_MS 100u

static volatile lv_display_t *s_disp_in_flight = NULL;
static volatile uint32_t s_flush_started_ms = 0;
static TS_StateTypeDef TS_State;

static void tft_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map);
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);

static void finish_flush(void) {
    lv_display_t *disp = (lv_display_t *)s_disp_in_flight;
    s_disp_in_flight = NULL;
    if (disp != NULL) {
        lv_display_flush_ready(disp);
    }
}

static void dma2d_complete(DMA2D_HandleTypeDef *dma2d) {
    (void)dma2d;
    finish_flush();
}

static void dma2d_error(DMA2D_HandleTypeDef *dma2d) {
    (void)dma2d;
    finish_flush();
}

void DMA2D_IRQHandler(void) {
    HAL_DMA2D_IRQHandler(&hdma2d_eval);
}

void tft_poll(void) {
    if (s_disp_in_flight == NULL ||
        HAL_GetTick() - s_flush_started_ms < DMA2D_TIMEOUT_MS) {
        return;
    }

    (void)HAL_DMA2D_Abort(&hdma2d_eval);
    finish_flush();
}

void tft_init(void) {
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(
        LTDC_ACTIVE_LAYER_BACKGROUND, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER_BACKGROUND);
    BSP_LCD_Clear(0xFFFFFFFF);
    BSP_LCD_SetBackColor(0xFFFFFFFF);

    hdma2d_eval.XferCpltCallback = dma2d_complete;
    hdma2d_eval.XferErrorCallback = dma2d_error;

    static lv_color_t buf1[LV_HOR_RES_MAX * 30];
    lv_display_t *disp = lv_display_create(
        LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, tft_flush);
    lv_display_set_buffers(
        disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

static void tft_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map) {
#if LV_COLOR_DEPTH == 32
    if (area->x2 < area->x1 || area->y2 < area->y1 || px_map == NULL) {
        lv_display_flush_ready(disp);
        return;
    }

    uint32_t width = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t height = (uint32_t)(area->y2 - area->y1 + 1);
    uint32_t bytes_per_pixel = lv_color_format_get_size(
        lv_display_get_color_format(disp));
    uint32_t row_bytes =
        (width * bytes_per_pixel + (LV_DRAW_BUF_STRIDE_ALIGN - 1u))
        & ~(uint32_t)(LV_DRAW_BUF_STRIDE_ALIGN - 1u);
    uint32_t source_pitch = row_bytes / bytes_per_pixel;
    uint32_t framebuffer =
        hltdc_eval.LayerCfg[LTDC_ACTIVE_LAYER_BACKGROUND].FBStartAdress;

    s_disp_in_flight = disp;
    s_flush_started_ms = HAL_GetTick();
    uint8_t result = BSP_LCD_DrawBitmapRaw_IT_To(
        framebuffer,
        area->x1, area->y1,
        width, height,
        bytes_per_pixel * 8u,
        px_map, source_pitch);
    if (result != LCD_OK) {
        finish_flush();
    }
#else
#error "Unsupported LV_COLOR_DEPTH"
#endif
}

void touchpad_init(void) {
    BSP_TS_Init(LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    (void)indev;
    BSP_TS_GetState(&TS_State);
    if (TS_State.touchDetected != 0) {
        data->point.x = TS_State.touchX[0];
        data->point.y = TS_State.touchY[0];
        last_x = data->point.x;
        last_y = data->point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
