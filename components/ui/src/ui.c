#include "ui.h"
#include "zotti_theme.h"
#include "zotti_brightness.h"
#include "esp_log.h"

static const char *TAG = "UI";

void ui_init(void)
{
    ESP_LOGI(TAG, "UI init");
    zotti_theme_init();
    zotti_brightness_init();
    ui_splash_show();
}
