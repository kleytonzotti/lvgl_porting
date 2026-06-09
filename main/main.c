#include "esp_log.h"
#include "bsp_waveshare_43.h"
#include "app_core.h"
#include "ui.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando projeto LVGL_PORTING organizado");

    ESP_ERROR_CHECK(bsp_waveshare_43_init());

    app_core_init();


    ui_init();

    ESP_LOGI(TAG, "Sistema iniciado");

    while (1) {
        bsp_lvgl_handler();
    }
}