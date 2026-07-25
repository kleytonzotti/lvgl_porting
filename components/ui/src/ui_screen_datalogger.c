#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_DATALOGGER";

static bool s_logging = false;
static lv_obj_t *s_btn_log = NULL;
static lv_obj_t *s_list    = NULL;
static lv_obj_t *s_lbl_count = NULL;
static int32_t s_entry_count = 0;

static void back_cb(lv_event_t *e)  { LV_UNUSED(e); ui_nav(ui_menu_show); }

static void toggle_log_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_logging = !s_logging;

    lv_obj_t *lbl = lv_obj_get_child(s_btn_log, 0);
    if (s_logging) {
        lv_label_set_text(lbl, LV_SYMBOL_STOP " PARAR");
        lv_obj_set_style_bg_color(s_btn_log, ZOTTI_RED, 0);
    } else {
        lv_label_set_text(lbl, LV_SYMBOL_PLAY " INICIAR");
        lv_obj_set_style_bg_color(s_btn_log, ZOTTI_GREEN, 0);
    }
}

static void clear_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_list) {
        lv_obj_clean(s_list);
        s_entry_count = 0;
        if (s_lbl_count) lv_label_set_text(s_lbl_count, "0 entradas");
    }
}

void ui_screen_datalogger_show(void)
{
    s_logging = false;
    s_btn_log = NULL;
    s_list    = NULL;
    s_entry_count = 0;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_label_set_text(lbl_title, "DATA LOGGER");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Barra de controles.
    lv_obj_t *ctrl = lv_obj_create(scr);
    lv_obj_set_size(ctrl, 800, 56);
    lv_obj_set_pos(ctrl, 0, 40);
    lv_obj_set_style_bg_color(ctrl, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(ctrl, 0, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);

    // Botao iniciar/parar.
    s_btn_log = lv_btn_create(ctrl);
    lv_obj_set_size(s_btn_log, 150, 36);
    lv_obj_align(s_btn_log, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(s_btn_log, ZOTTI_GREEN, 0);
    lv_obj_set_style_radius(s_btn_log, 6, 0);
    lv_obj_add_event_cb(s_btn_log, toggle_log_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_log = lv_label_create(s_btn_log);
    lv_label_set_text(lbl_log, LV_SYMBOL_PLAY " INICIAR");
    lv_obj_set_style_text_font(lbl_log, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_log);

    // Botao limpar.
    lv_obj_t *btn_clear = lv_btn_create(ctrl);
    lv_obj_set_size(btn_clear, 120, 36);
    lv_obj_align(btn_clear, LV_ALIGN_LEFT_MID, 175, 0);
    lv_obj_set_style_bg_color(btn_clear, ZOTTI_GRAY_DARK, 0);
    lv_obj_set_style_radius(btn_clear, 6, 0);
    lv_obj_add_event_cb(btn_clear, clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_clear = lv_label_create(btn_clear);
    lv_label_set_text(lbl_clear, LV_SYMBOL_TRASH " Limpar");
    lv_obj_set_style_text_font(lbl_clear, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_clear);

    // Contador
    s_lbl_count = lv_label_create(ctrl);
    lv_label_set_text(s_lbl_count, "0 entradas");
    lv_obj_set_style_text_font(s_lbl_count, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_count, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_count, LV_ALIGN_RIGHT_MID, -15, 0);

    // Canais
    //lv_obj_t *lbl_ch = lv_label_create(ctrl);
    //lv_label_set_text(lbl_ch, "Canais: RPM | TPS | MAP | AFR | Lambda | Vel | ECT | IAT");
    //lv_obj_set_style_text_font(lbl_ch, ZOTTI_FONT_TINY, 0);
    //lv_obj_set_style_text_color(lbl_ch, ZOTTI_ACCENT, 0);
    //lv_obj_align(lbl_ch, LV_ALIGN_BOTTOM_LEFT, 10, -5);

    // Cabecalho da lista.
    lv_obj_t *list_hdr = lv_obj_create(scr);
    lv_obj_set_size(list_hdr, 800, 28);
    lv_obj_set_pos(list_hdr, 0, 96);
    lv_obj_set_style_bg_color(list_hdr, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(list_hdr, 0, 0);
    lv_obj_clear_flag(list_hdr, LV_OBJ_FLAG_SCROLLABLE);

    const char *hdr_labels[] = { "Tempo(ms)", "RPM", "TPS%", "MAP", "AFR", "Vel", "ECT", "IAT" };
    int32_t hdr_x[]          = {  5,          90,    145,    200,   260,   310,   365,   420  };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *lbl = lv_label_create(list_hdr);
        lv_label_set_text(lbl, hdr_labels[i]);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, hdr_x[i], 0);
    }

    // Lista scrollavel de entradas.
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 800, 356);
    lv_obj_set_pos(s_list, 0, 124);
    lv_obj_set_style_bg_color(s_list, ZOTTI_BG, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);

    lv_obj_t *lbl_empty = lv_label_create(s_list);
    lv_label_set_text(lbl_empty, "Inicie o Data Logger para capturar dados.");
    lv_obj_set_style_text_color(lbl_empty, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl_empty, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "DataLogger criado");
}
