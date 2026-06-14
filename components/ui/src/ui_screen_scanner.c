#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_SCANNER";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_menu_show(); }

static void connect_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    // TODO: iniciar scan BLE por ELM327
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Buscando...");
}

void ui_screen_scanner_show(void)
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
    lv_label_set_text(lbl_title, "SCANNER OBD2");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Área de status ────────────────────────────────────────
    lv_obj_t *status_card = lv_obj_create(scr);
    lv_obj_set_size(status_card, 760, 90);
    lv_obj_align(status_card, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_color(status_card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(status_card, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_radius(status_card, 6, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_status = lv_label_create(status_card);
    lv_label_set_text(lbl_status, LV_SYMBOL_BLUETOOTH "  Aguardando ELM327 via BLE...");
    lv_obj_set_style_text_font(lbl_status, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_status, ZOTTI_YELLOW, 0);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 15, -10);

    lv_obj_t *lbl_proto = lv_label_create(status_card);
    lv_label_set_text(lbl_proto, "Protocolo: ---   VIN: ---");
    lv_obj_set_style_text_font(lbl_proto, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_proto, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_proto, LV_ALIGN_LEFT_MID, 15, 15);

    // ── Botão conectar ────────────────────────────────────────
    lv_obj_t *btn_conn = lv_btn_create(scr);
    lv_obj_set_size(btn_conn, 200, 44);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_RIGHT, -20, 155);
    lv_obj_set_style_bg_color(btn_conn, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_conn, 6, 0);
    lv_obj_add_event_cb(btn_conn, connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, LV_SYMBOL_BLUETOOTH " Conectar ELM327");
    lv_obj_set_style_text_font(lbl_conn, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_conn);

    // ── Abas: Sensores | Falhas | Monitores ──────────────────
    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, 800, 280);
    lv_obj_set_pos(tv, 0, 205);
    lv_tabview_set_tab_bar_size(tv, 36);
    lv_obj_set_style_bg_color(tv, ZOTTI_BG, 0);

    lv_obj_t *tab_sens = lv_tabview_add_tab(tv, "Sensores");
    lv_obj_t *tab_dtc  = lv_tabview_add_tab(tv, "Falhas (DTC)");
    lv_obj_t *tab_mon  = lv_tabview_add_tab(tv, "Monitores");

    // Tab sensores — placeholder
    lv_obj_t *lbl_s = lv_label_create(tab_sens);
    lv_label_set_text(lbl_s, "Conecte ao ELM327 para ver os sensores do veículo.");
    lv_obj_set_style_text_color(lbl_s, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl_s, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl_s, LV_ALIGN_CENTER, 0, 0);

    // Tab DTC — placeholder
    lv_obj_t *lbl_d = lv_label_create(tab_dtc);
    lv_label_set_text(lbl_d, "Nenhuma falha lida.\nConecte ao ELM327 e clique em Conectar.");
    lv_obj_set_style_text_color(lbl_d, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl_d, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl_d, LV_ALIGN_CENTER, 0, 0);

    // Tab monitores — placeholder
    lv_obj_t *lbl_m = lv_label_create(tab_mon);
    lv_label_set_text(lbl_m, "Monitores OBD2 serão exibidos aqui.");
    lv_obj_set_style_text_color(lbl_m, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl_m, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl_m, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Scanner criado");
}
