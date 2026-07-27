#include "bsp_waveshare_43.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "BSP_BL";

static uint8_t          s_ch422_state = BSP_CH422_TP_RST_BIT | BSP_CH422_SD_CS_BIT | (1U << 3);
static SemaphoreHandle_t s_ch422_lock;

static void ensure_ch422_lock(void)
{
    if (!s_ch422_lock) {
        s_ch422_lock = xSemaphoreCreateMutex();
    }
}

static esp_err_t bsp_ch422_write(uint8_t value)
{
    uint8_t cfg = 0x01;
    esp_err_t err = i2c_master_write_to_device(BSP_I2C_NUM, 0x24, &cfg, 1,
                                               pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_write_to_device(BSP_I2C_NUM, 0x38, &value, 1,
                                     pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    if (err == ESP_OK) {
        s_ch422_state = value;
    }
    return err;
}

// Protegido por mutex: SD/CAN/touch/backlight compartilham o mesmo byte de
// estado do CH422G (leitura-modificacao-escrita) — sem isso, duas chamadas
// concorrentes podem perder o bit uma da outra.
esp_err_t bsp_ch422_update(uint8_t mask, uint8_t value)
{
    ensure_ch422_lock();
    xSemaphoreTake(s_ch422_lock, portMAX_DELAY);
    uint8_t next = (s_ch422_state & (uint8_t)~mask) | (value & mask);
    esp_err_t err = bsp_ch422_write(next);
    xSemaphoreGive(s_ch422_lock);
    return err;
}

esp_err_t bsp_backlight_set(bool enable)
{
    ESP_ERROR_CHECK(bsp_ch422_update(BSP_CH422_LCD_BL_BIT,
                                     enable ? BSP_CH422_LCD_BL_BIT : 0));

    ESP_LOGI(TAG, "Backlight %s", enable ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t bsp_sdcard_select(bool selected)
{
    return bsp_ch422_update(BSP_CH422_SD_CS_BIT, selected ? 0 : BSP_CH422_SD_CS_BIT);
}

esp_err_t bsp_can_set_selected(bool selected)
{
    return bsp_ch422_update(BSP_CH422_CAN_SEL_BIT,
                            selected ? BSP_CH422_CAN_SEL_BIT : 0);
}

esp_err_t bsp_touch_reset_set(bool high)
{
    return bsp_ch422_update(BSP_CH422_TP_RST_BIT,
                            high ? BSP_CH422_TP_RST_BIT : 0);
}
