#include "bsp_waveshare_43.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "BSP_BL";

esp_err_t bsp_backlight_set(bool enable)
{
    uint8_t buf;

    // CH422G: configura pinos como saída
    buf = 0x01;
    i2c_master_write_to_device(BSP_I2C_NUM, 0x24, &buf, 1,
        pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));

    // Liga ou apaga o backlight
    buf = enable ? 0x1E : 0x1A;
    ESP_ERROR_CHECK(i2c_master_write_to_device(BSP_I2C_NUM, 0x38, &buf, 1,
        pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS)));

    ESP_LOGI(TAG, "Backlight %s", enable ? "ON" : "OFF");
    return ESP_OK;
}