#include "bsp_waveshare_43.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "BSP_I2C";

esp_err_t bsp_i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BSP_I2C_SDA,
        .scl_io_num       = BSP_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BSP_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(BSP_I2C_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(BSP_I2C_NUM, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C inicializado (SDA=%d SCL=%d)", BSP_I2C_SDA, BSP_I2C_SCL);
    return ESP_OK;
}