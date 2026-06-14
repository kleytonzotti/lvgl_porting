#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_ECU";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_menu_show(); }

// Sensores recebidos via BLE da ECU externa
typedef struct {
    const char *name;
    const char *unit;
    lv_color_t  color;
} ecu_sensor_t;

static const ecu_sensor_t k_sensors[] = {
    { "RPM",      "rpm",  {0} },
    { "MAP",      "kPa",  {0} },
    { "TPS",      "%",    {0} },
    { "AFR",      "",     {0} },
    { "Lambda",   "λ",   {0} },
    { "ECT",      "°C",  {0} },
    { "IAT",      "°C",  {0} },
    { "Pressão",  "kPa",  {0} },
    { "Bateria",  "V",    {0} },
    { "Estado",   "",     {0} },
};

void ui_screen_ecu_show(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header ───────────────────────────────────────────────
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 90, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " MENU");
    lv_obj_set_style_text_font(lbl_back, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_back);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "MONITOR ECU BLE");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Status conexão BLE ────────────────────────────────────
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 36);
    lv_obj_set_pos(status_bar, 0, 40);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_ble = lv_label_create(status_bar);
    lv_label_set_text(lbl_ble, LV_SYMBOL_BLUETOOTH "  ECU BLE: DESCONECTADA");
    lv_obj_set_style_text_font(lbl_ble, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_ble, ZOTTI_RED, 0);
    lv_obj_align(lbl_ble, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *lbl_proto = lv_label_create(status_bar);
    lv_label_set_text(lbl_proto, "Comando: WB1 via BLE para ativar");
    lv_obj_set_style_text_font(lbl_proto, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_proto, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_proto, LV_ALIGN_RIGHT_MID, -15, 0);

    // ── Grade de sensores (2 colunas × 5 linhas) ──────────────
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 800, 400);
    lv_obj_set_pos(grid, 0, 80);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP, 0);
    lv_obj_set_style_flex_main_place(grid, LV_FLEX_ALIGN_START, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    int32_t num_sensors = sizeof(k_sensors) / sizeof(k_sensors[0]);
    for (int32_t i = 0; i < num_sensors; i++) {
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, 375, 68);
        lv_obj_set_style_bg_color(card, ZOTTI_BG_CARD, 0);
        lv_obj_set_style_border_color(card, ZOTTI_BORDER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // Indicador lateral
        lv_obj_t *ind = lv_obj_create(card);
        lv_obj_set_size(ind, 4, 68);
        lv_obj_set_pos(ind, 0, 0);
        lv_obj_set_style_bg_color(ind, ZOTTI_ACCENT_DIM, 0);
        lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ind, 0, 0);
        lv_obj_set_style_radius(ind, 0, 0);
        lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, k_sensors[i].name);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 14, 8);

        lv_obj_t *lbl_val = lv_label_create(card);
        lv_label_set_text(lbl_val, "---");
        lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_LARGE, 0);
        lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
        lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_LEFT, 14, -8);

        lv_obj_t *lbl_unit = lv_label_create(card);
        lv_label_set_text(lbl_unit, k_sensors[i].unit);
        lv_obj_set_style_text_font(lbl_unit, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_unit, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "ECU criado");
}
