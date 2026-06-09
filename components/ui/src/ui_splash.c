#include "ui.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_SPLASH";

void ui_splash_create(void)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL OK");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    ESP_LOGI(TAG, "Splash criado");
}