#include "ui.h"
#include "esp_log.h"

static const char *TAG = "UI";

void ui_init(void)
{
    ESP_LOGI(TAG, "UI init");
    ui_tabs_create();
    ESP_LOGI(TAG, "Tela ativa: %p, num filhos: %d",
             lv_scr_act(), (int)lv_obj_get_child_cnt(lv_scr_act()));
}