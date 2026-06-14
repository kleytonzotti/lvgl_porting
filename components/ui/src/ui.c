#include "ui.h"
#include "esp_log.h"

static const char *TAG = "UI";

void ui_init(void)
{
    ESP_LOGI(TAG, "UI init");
    ui_splash_show();
}
