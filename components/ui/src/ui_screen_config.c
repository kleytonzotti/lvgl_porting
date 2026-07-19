#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_ble.h"

static const char *TAG = "UI_CONFIG";

static lv_obj_t *s_scr;

// ─────────────────────────────────────────────────────
// Popup: lista de dispositivos BLE encontrados no scan
// ─────────────────────────────────────────────────────

static lv_obj_t *s_ble_list_cont;
static lv_obj_t *s_ble_status_lbl;
static lv_timer_t *s_ble_timer;
static app_ble_scan_result_t s_ble_devices[APP_BLE_MAX_SCAN_RESULTS];
static uint32_t s_ble_device_count;

static void ble_device_row_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    if (idx >= s_ble_device_count) return;
    app_ble_connect(&s_ble_devices[idx]);
}

static void ble_rebuild_list(void)
{
    s_ble_device_count = app_ble_get_scan_results(s_ble_devices, APP_BLE_MAX_SCAN_RESULTS);

    lv_obj_clean(s_ble_list_cont);
    for (uint32_t i = 0; i < s_ble_device_count; i++) {
        app_ble_scan_result_t *dev = &s_ble_devices[i];

        lv_obj_t *row = lv_obj_create(s_ble_list_cont);
        lv_obj_set_size(row, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(row, ZOTTI_BG_HEADER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, ZOTTI_BORDER, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, ble_device_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s   %s   %ddBm", dev->name, dev->addr_str, (int)dev->rssi);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_WHITE, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    }

    if (s_ble_device_count == 0) {
        lv_obj_t *lbl = lv_label_create(s_ble_list_cont);
        lv_label_set_text(lbl, "Nenhum dispositivo encontrado...");
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
    }
}

static void ble_popup_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ble_rebuild_list();

    app_ble_status_t st;
    app_ble_get_status(&st);
    lv_label_set_text(s_ble_status_lbl, st.status_text);
    lv_obj_set_style_text_color(s_ble_status_lbl, ZOTTI_ACCENT, 0);
}

static void ble_popup_close_cb(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);

    app_ble_scan_stop();
    if (s_ble_timer) {
        lv_timer_delete(s_ble_timer);
        s_ble_timer = NULL;
    }
    s_ble_list_cont = NULL;
    s_ble_status_lbl = NULL;

    if (overlay) lv_obj_delete(overlay);
}

static void open_ble_scan_dialog(void)
{
    if (!s_scr) return;

    lv_obj_t *overlay = lv_obj_create(s_scr);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(overlay);
    lv_obj_set_size(dlg, 520, 380);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0D1F33), 0);
    lv_obj_set_style_border_color(dlg, ZOTTI_ACCENT, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_radius(dlg, 8, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(dlg, 10, 0);

    lv_obj_t *lbl_title = lv_label_create(dlg);
    lv_label_set_text(lbl_title, LV_SYMBOL_BLUETOOTH "  DISPOSITIVOS BLE");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ble_status_lbl = lv_label_create(dlg);
    lv_label_set_text(s_ble_status_lbl, "Escaneando...");
    lv_obj_set_style_text_font(s_ble_status_lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ble_status_lbl, ZOTTI_ACCENT, 0);
    lv_obj_align(s_ble_status_lbl, LV_ALIGN_TOP_LEFT, 0, 28);

    s_ble_list_cont = lv_obj_create(dlg);
    lv_obj_set_size(s_ble_list_cont, 500, 270);
    lv_obj_align(s_ble_list_cont, LV_ALIGN_TOP_LEFT, 0, 54);
    lv_obj_set_style_bg_color(s_ble_list_cont, lv_color_hex(0x061525), 0);
    lv_obj_set_style_border_width(s_ble_list_cont, 0, 0);
    lv_obj_set_style_pad_all(s_ble_list_cont, 6, 0);
    lv_obj_set_style_pad_row(s_ble_list_cont, 6, 0);
    lv_obj_set_layout(s_ble_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(s_ble_list_cont, LV_FLEX_FLOW_COLUMN, 0);

    lv_obj_t *btn_close = lv_btn_create(dlg);
    lv_obj_set_size(btn_close, 150, 34);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_close, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_close, 4, 0);
    lv_obj_add_event_cb(btn_close, ble_popup_close_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Fechar");
    lv_obj_set_style_text_font(lbl_close, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_close);

    s_ble_device_count = 0;
    ble_rebuild_list();
    app_ble_scan_start();

    s_ble_timer = lv_timer_create(ble_popup_timer_cb, 500, NULL);
}

static void ble_row_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    open_ble_scan_dialog();
}

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

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
    s_scr = scr;

    // Header.
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
    lv_label_set_text(lbl_title, "CONFIGURACOES");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Area scrollavel de configuracoes.
    lv_obj_t *scroll = lv_obj_create(scr);
    lv_obj_set_size(scroll, 800, 440);
    lv_obj_set_pos(scroll, 0, 40);
    lv_obj_set_style_bg_color(scroll, ZOTTI_BG, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 20, 0);
    lv_obj_set_style_pad_row(scroll, 10, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(scroll, LV_FLEX_FLOW_COLUMN, 0);

    // Secao: Display.
    add_section_label(scroll, "  DISPLAY");

    // Tema (dropdown)
    lv_obj_t *row_tema = add_row(scroll, "Tema");
    lv_obj_t *dd_tema = lv_dropdown_create(row_tema);
    lv_dropdown_set_options(dd_tema, "ZOTTI DARK\nZOTTI BLUE\nZOTTI LIGHT");
    lv_obj_set_size(dd_tema, 200, 34);
    lv_obj_align(dd_tema, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_tema, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_tema, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_tema, ZOTTI_FONT_TINY, 0);

    // Secao: Comunicacao.
    add_section_label(scroll, "  COMUNICACAO");

    // BLE
    lv_obj_t *row_ble = add_row(scroll, LV_SYMBOL_BLUETOOTH "  Bluetooth (BLE)");
    lv_obj_t *sw_ble = lv_switch_create(row_ble);
    lv_obj_align(sw_ble, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(sw_ble, ZOTTI_ACCENT, LV_STATE_CHECKED);
    lv_obj_add_state(sw_ble, LV_STATE_CHECKED);

    lv_obj_t *btn_ble_scan = lv_btn_create(row_ble);
    lv_obj_set_size(btn_ble_scan, 150, 34);
    lv_obj_align(btn_ble_scan, LV_ALIGN_RIGHT_MID, -90, 0);
    lv_obj_set_style_bg_color(btn_ble_scan, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_ble_scan, 4, 0);
    lv_obj_add_event_cb(btn_ble_scan, ble_row_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ble_scan = lv_label_create(btn_ble_scan);
    lv_label_set_text(lbl_ble_scan, LV_SYMBOL_LIST " Dispositivos");
    lv_obj_set_style_text_font(lbl_ble_scan, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_ble_scan);

    // WiFi
    lv_obj_t *row_wifi = add_row(scroll, LV_SYMBOL_WIFI "  WiFi (AP)");
    lv_obj_t *sw_wifi = lv_switch_create(row_wifi);
    lv_obj_align(sw_wifi, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(sw_wifi, ZOTTI_ACCENT, LV_STATE_CHECKED);

    // Secao: Sistema.
    add_section_label(scroll, "  SISTEMA");

    // Idioma
    lv_obj_t *row_idioma = add_row(scroll, "Idioma");
    lv_obj_t *dd_idioma = lv_dropdown_create(row_idioma);
    lv_dropdown_set_options(dd_idioma, "Portugues\nEnglish\nEspanol");
    lv_obj_set_size(dd_idioma, 200, 34);
    lv_obj_align(dd_idioma, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_idioma, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_idioma, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_idioma, ZOTTI_FONT_TINY, 0);

    // OTA
    lv_obj_t *row_ota = add_row(scroll, LV_SYMBOL_REFRESH "  Atualizacao OTA");
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
