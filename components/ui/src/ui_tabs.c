#include "ui.h"
#include "esp_log.h"
#include "bsp_waveshare_43.h"

static const char *TAG = "UI_TABS";

void ui_tabs_create(void)
{
    lv_obj_t *tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 40);

    lv_obj_t *tab_anim    = lv_tabview_add_tab(tv, "Motor");
    lv_obj_t *tab_entr    = lv_tabview_add_tab(tv, "Entradas");
    lv_obj_t *tab_said    = lv_tabview_add_tab(tv, "Saidas");
    lv_obj_t *tab_calc    = lv_tabview_add_tab(tv, "Calculos");
    lv_obj_t *tab_bot     = lv_tabview_add_tab(tv, "Botoes");
    lv_obj_t *tab_touch   = lv_tabview_add_tab(tv, "Touch");

    ui_screen_animacao_create(tab_anim);
    ui_screen_entradas_create(tab_entr);
    ui_screen_saidas_create(tab_said);
    ui_screen_calculos_create(tab_calc);
    ui_screen_botoes_create(tab_bot);
    ui_screen_touch_create(tab_touch);

    ESP_LOGI(TAG, "Tabs criadas");
}