#include "bsp_waveshare_43.h"
#include "esp_log.h"

static const char *TAG = "BSP_BACKLIGHT";

esp_err_t bsp_backlight_set(bool enable)
{
    ESP_LOGI(TAG, "Backlight: %s", enable ? "ON" : "OFF");
    return ESP_OK;
}