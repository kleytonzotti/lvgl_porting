#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_CONFIG";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_menu_show(); }

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    // TODO: chamar bsp_backlight_set_level(val)
    ESP_LOGI("CONFIG", "Brilho: %ld", (long)val);
}

static lv_obj_t *add_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl, ZOTTI_ACCENT, 0);
    return lbl;
}

static lv_obj_t *add_row(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 740, 52);
    lv_obj_set_style_bg_color(row, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(row, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl, ZOTTI_WHITE, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

    return row;
}

void ui_screen_config_show(void)
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
    lv_label_set_text(lbl_title, "CONFIGURAÇÕES");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Área scrollável de configurações ──────────────────────
    lv_obj_t *scroll = lv_obj_create(scr);
    lv_obj_set_size(scroll, 800, 440);
    lv_obj_set_pos(scroll, 0, 40);
    lv_obj_set_style_bg_color(scroll, ZOTTI_BG, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 20, 0);
    lv_obj_set_style_pad_row(scroll, 10, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(scroll, LV_FLEX_FLOW_COLUMN, 0);

    // ── Seção: Display ────────────────────────────────────────
    add_section_label(scroll, "  DISPLAY");

    // Brilho
    lv_obj_t *row_brilho = add_row(scroll, "Brilho");
    lv_obj_t *slider = lv_slider_create(row_brilho);
    lv_obj_set_size(slider, 280, 6);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ZOTTI_WHITE, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Tema (dropdown)
    lv_obj_t *row_tema = add_row(scroll, "Tema");
    lv_obj_t *dd_tema = lv_dropdown_create(row_tema);
    lv_dropdown_set_options(dd_tema, "ZOTTI DARK\nZOTTI BLUE\nZOTTI LIGHT");
    lv_obj_set_size(dd_tema, 200, 34);
    lv_obj_align(dd_tema, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_tema, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_tema, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_tema, ZOTTI_FONT_TINY, 0);

    // ── Seção: Comunicação ────────────────────────────────────
    add_section_label(scroll, "  COMUNICAÇÃO");

    // BLE
    lv_obj_t *row_ble = add_row(scroll, LV_SYMBOL_BLUETOOTH "  Bluetooth (BLE)");
    lv_obj_t *sw_ble = lv_switch_create(row_ble);
    lv_obj_align(sw_ble, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(sw_ble, ZOTTI_ACCENT, LV_STATE_CHECKED);
    lv_obj_add_state(sw_ble, LV_STATE_CHECKED);

    // WiFi
    lv_obj_t *row_wifi = add_row(scroll, LV_SYMBOL_WIFI "  WiFi (AP)");
    lv_obj_t *sw_wifi = lv_switch_create(row_wifi);
    lv_obj_align(sw_wifi, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(sw_wifi, ZOTTI_ACCENT, LV_STATE_CHECKED);

    // ── Seção: Sistema ────────────────────────────────────────
    add_section_label(scroll, "  SISTEMA");

    // Idioma
    lv_obj_t *row_idioma = add_row(scroll, "Idioma");
    lv_obj_t *dd_idioma = lv_dropdown_create(row_idioma);
    lv_dropdown_set_options(dd_idioma, "Português\nEnglish\nEspañol");
    lv_obj_set_size(dd_idioma, 200, 34);
    lv_obj_align(dd_idioma, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_idioma, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_idioma, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_idioma, ZOTTI_FONT_TINY, 0);

    // OTA
    lv_obj_t *row_ota = add_row(scroll, LV_SYMBOL_REFRESH "  Atualização OTA");
    lv_obj_t *btn_ota = lv_btn_create(row_ota);
    lv_obj_set_size(btn_ota, 150, 34);
    lv_obj_align(btn_ota, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_ota, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_ota, 4, 0);
    lv_obj_t *lbl_ota = lv_label_create(btn_ota);
    lv_label_set_text(lbl_ota, "Verificar Update");
    lv_obj_set_style_text_font(lbl_ota, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_ota);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Config criado");
}
