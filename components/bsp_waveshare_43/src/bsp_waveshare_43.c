#include "bsp_waveshare_43.h"
#include "esp_check.h"

static const char *TAG = "BSP_WAVESHARE";

esp_err_t bsp_waveshare_43_init(void)
{
    ESP_LOGI(TAG, "Iniciando BSP Waveshare 4.3\"");

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Erro ao iniciar I2C");

    // Backlight apagado durante init: evita exibir lixo do bounce buffer
    // antes do LVGL renderizar o primeiro frame
    ESP_RETURN_ON_ERROR(bsp_backlight_set(false), TAG, "Erro ao configurar backlight");
    ESP_LOGI(TAG, "Backlight: apagado (aguardando primeiro frame LVGL)");

    ESP_RETURN_ON_ERROR(bsp_lcd_rgb_init(), TAG, "Erro ao iniciar LCD RGB");
    ESP_RETURN_ON_ERROR(bsp_lvgl_init(), TAG, "Erro ao iniciar LVGL");

    ESP_LOGI(TAG, "BSP inicializado. Backlight sera ligado apos primeiro frame.");
    return ESP_OK;
}