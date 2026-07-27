#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_waveshare_43.h"
#include "app_core.h"
#include "ui.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ZOTTI INFO - boot ===");
    ESP_LOGI(TAG, "LVGL %d.%d.%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    // 1) BSP: I2C + LCD + LVGL (backlight apagado ate o 1o frame)
    ESP_LOGI(TAG, "[1/4] BSP init...");
    ESP_ERROR_CHECK(bsp_waveshare_43_init());
    ESP_LOGI(TAG, "[1/4] BSP OK");

    // 2) App core (CAN, BLE, etc.)
    ESP_LOGI(TAG, "[2/4] App core init...");
    app_core_init();
    ESP_LOGI(TAG, "[2/4] App core OK");

    // 3) UI: cria a splash screen dentro do mutex LVGL.
    ESP_LOGI(TAG, "[3/4] UI init...");
    if (bsp_lvgl_lock(-1)) {
        ui_init();
        bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG, "[3/4] UI criada, aguardando primeiro frame...");

    // 4) Aguarda o LVGL renderizar o primeiro frame antes de ligar o backlight.
    //    Com FREERTOS_HZ=100, 1 tick = 10ms. 10 ticks = 100ms garante
    //    que o primeiro frame (splash) ja esta no framebuffer PSRAM.
    vTaskDelay(10);
    ESP_ERROR_CHECK(bsp_backlight_set(true));
    ESP_LOGI(TAG, "[4/4] Backlight ON - sistema iniciado");

    while (1) {
        bsp_lvgl_handler();
    }
}