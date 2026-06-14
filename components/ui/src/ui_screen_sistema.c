#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "bsp_waveshare_43.h"
#include "esp_log.h"
#include "esp_idf_version.h"

static const char *TAG = "UI_SISTEMA";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_menu_show(); }

// Volta do touch diagnostic → limpa o callback antes de sair
static void touch_diag_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    bsp_touch_register_cb(NULL);
    ui_screen_sistema_show();
}

static void touch_diag_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Header com botão voltar
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, touch_diag_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b = lv_label_create(btn_back);
    lv_label_set_text(lbl_b, LV_SYMBOL_LEFT " SISTEMA");
    lv_obj_set_style_text_font(lbl_b, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_b);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "DIAGNÓSTICO DE TOUCH");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Conteúdo: container abaixo do header (800 × 440)
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, 800, 440);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_set_style_bg_color(content, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Reutiliza a tela de touch já implementada como filho do content
    ui_screen_touch_create(content);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

static lv_obj_t *add_info_row(lv_obj_t *parent, const char *key, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 740, 46);
    lv_obj_set_style_bg_color(row, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(row, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_key = lv_label_create(row);
    lv_label_set_text(lbl_key, key);
    lv_obj_set_style_text_font(lbl_key, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_key, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_key, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *lbl_val = lv_label_create(row);
    lv_label_set_text(lbl_val, value);
    lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_val, LV_ALIGN_RIGHT_MID, -10, 0);

    return lbl_val;
}

void ui_screen_sistema_show(void)
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
    lv_label_set_text(lbl_title, "SISTEMA");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Área scrollável ───────────────────────────────────────
    lv_obj_t *scroll = lv_obj_create(scr);
    lv_obj_set_size(scroll, 800, 440);
    lv_obj_set_pos(scroll, 0, 40);
    lv_obj_set_style_bg_color(scroll, ZOTTI_BG, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 20, 0);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(scroll, LV_FLEX_FLOW_COLUMN, 0);

    // ── Logo e versão ─────────────────────────────────────────
    lv_obj_t *logo_card = lv_obj_create(scroll);
    lv_obj_set_size(logo_card, 740, 70);
    lv_obj_set_style_bg_color(logo_card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(logo_card, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_border_width(logo_card, 1, 0);
    lv_obj_set_style_radius(logo_card, 6, 0);
    lv_obj_clear_flag(logo_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_logo = lv_label_create(logo_card);
    lv_label_set_text(lbl_logo, "ZOTTI INFO");
    lv_obj_set_style_text_font(lbl_logo, ZOTTI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(lbl_logo, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_logo, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *lbl_fw_ver = lv_label_create(logo_card);
    lv_label_set_text(lbl_fw_ver, "Firmware v1.0.0\nAutomotive Intelligence Platform");
    lv_obj_set_style_text_font(lbl_fw_ver, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_fw_ver, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_fw_ver, LV_ALIGN_RIGHT_MID, -10, 0);

    // ── Informações do hardware ───────────────────────────────
    add_info_row(scroll, "Plataforma",    "ESP32-S3 Touch LCD 4.3\"");
    add_info_row(scroll, "Display",       "800 × 480 px  |  RGB565");
    add_info_row(scroll, "Touch",         "GT911 Capacitivo (I2C)");
    add_info_row(scroll, "PSRAM",         "8 MB");
    add_info_row(scroll, "Flash",         "16 MB");
    add_info_row(scroll, "ESP-IDF",       esp_get_idf_version());
    add_info_row(scroll, "LVGL",          "v9.4.0");

    // ── Diagnósticos ──────────────────────────────────────────
    lv_obj_t *lbl_sec = lv_label_create(scroll);
    lv_label_set_text(lbl_sec, "  DIAGNÓSTICOS");
    lv_obj_set_style_text_font(lbl_sec, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_sec, ZOTTI_ACCENT, 0);

    // Botão diagnóstico de touch
    lv_obj_t *row_touch = lv_obj_create(scroll);
    lv_obj_set_size(row_touch, 740, 46);
    lv_obj_set_style_bg_color(row_touch, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(row_touch, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(row_touch, 1, 0);
    lv_obj_set_style_radius(row_touch, 6, 0);
    lv_obj_clear_flag(row_touch, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(row_touch);
    lv_label_set_text(lbl_t, "Diagnóstico de Touch");
    lv_obj_set_style_text_font(lbl_t, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_t, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_t, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *btn_touch = lv_btn_create(row_touch);
    lv_obj_set_size(btn_touch, 100, 30);
    lv_obj_align(btn_touch, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_touch, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_touch, 4, 0);
    lv_obj_add_event_cb(btn_touch, touch_diag_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn_t = lv_label_create(btn_touch);
    lv_label_set_text(lbl_btn_t, "Abrir");
    lv_obj_set_style_text_font(lbl_btn_t, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_btn_t);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Sistema criado");
}
