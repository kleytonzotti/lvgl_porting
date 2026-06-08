#include "bsp_waveshare_43.h"
#include "esp_check.h"

static const char *TAG = "BSP_WAVESHARE";

esp_err_t bsp_waveshare_43_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Erro ao iniciar I2C");
    ESP_RETURN_ON_ERROR(bsp_backlight_set(true), TAG, "Erro ao ligar backlight");
    ESP_RETURN_ON_ERROR(bsp_lcd_rgb_init(), TAG, "Erro ao iniciar LCD RGB");
    ESP_RETURN_ON_ERROR(bsp_lvgl_init(), TAG, "Erro ao iniciar LVGL");

    return ESP_OK;
}