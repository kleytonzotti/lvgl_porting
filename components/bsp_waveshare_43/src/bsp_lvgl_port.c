#include "bsp_waveshare_43.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_LVGL";

esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "LVGL init placeholder");
    return ESP_OK;
}

void bsp_lvgl_handler(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
}