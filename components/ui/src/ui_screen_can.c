#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_CAN";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

// Abas internas do modulo CAN.
static void build_sniffer_tab(lv_obj_t *parent)
{
    // Cabecalho da tabela.
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, 740, 32);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(hdr, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    const char *cols[] = { "ID (hex)", "DLC", "DATA (hex)", "Freq" };
    int32_t col_x[]    = {  5,          120,   170,          550  };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(hdr);
        lv_label_set_text(lbl, cols[i]);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, col_x[i], 0);
    }

    // Placeholder de mensagem
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Aguardando mensagens CAN...\n\nConecte o barramento CAN para iniciar captura.");
    lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 30);
}

static void build_decoder_tab(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Decodificador CAN\n\n"
                           "Interpreta sinais do barramento:\n"
                           "  RPM, Velocidade, Temperatura, Combustivel...\n\n"
                           "Selecione o perfil do veiculo em Configuracoes.");
    lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 10);
}

static void build_gateway_tab(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "CAN Gateway\n\n"
                           "Recebe mensagens -> Interpreta -> Encaminha.\n\n"
                           "Status: INATIVO");
    lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 10);
}

void ui_screen_can_show(void)
{
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
    lv_label_set_text(lbl_title, "MONITOR CAN");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Status da interface CAN
    lv_obj_t *lbl_can_status = lv_label_create(header);
    lv_label_set_text(lbl_can_status, "CAN: OFF");
    lv_obj_set_style_text_font(lbl_can_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_can_status, ZOTTI_RED, 0);
    lv_obj_align(lbl_can_status, LV_ALIGN_RIGHT_MID, -10, 0);

    // Filtros.
    lv_obj_t *filter_bar = lv_obj_create(scr);
    lv_obj_set_size(filter_bar, 800, 44);
    lv_obj_set_pos(filter_bar, 0, 40);
    lv_obj_set_style_bg_color(filter_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(filter_bar, 0, 0);
    lv_obj_set_layout(filter_bar, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(filter_bar, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_pad_all(filter_bar, 5, 0);
    lv_obj_set_style_pad_column(filter_bar, 6, 0);
    lv_obj_set_style_flex_main_place(filter_bar, LV_FLEX_ALIGN_CENTER, 0);

    const char *filtros[] = { "Todos", "Motor", "Painel", "BCM", "ABS", "Airbag" };
    for (int i = 0; i < 6; i++) {
        lv_obj_t *btn = lv_btn_create(filter_bar);
        lv_obj_set_size(btn, 110, 32);
        lv_obj_set_style_bg_color(btn, (i == 0) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_HEADER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, filtros[i]);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_center(lbl);
    }

    // Tabview: Sniffer | Decoder | Gateway.
    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, 800, 312);
    lv_obj_set_pos(tv, 0, 168);
    lv_tabview_set_tab_bar_size(tv, 36);
    lv_obj_set_style_bg_color(tv, ZOTTI_BG, 0);

    lv_obj_t *tab_sniff   = lv_tabview_add_tab(tv, "Sniffer");
    lv_obj_t *tab_decode  = lv_tabview_add_tab(tv, "Decoder");
    lv_obj_t *tab_gateway = lv_tabview_add_tab(tv, "Gateway");

    build_sniffer_tab(tab_sniff);
    build_decoder_tab(tab_decode);
    build_gateway_tab(tab_gateway);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "CAN criado");
}
