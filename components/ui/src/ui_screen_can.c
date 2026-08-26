#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_CAN";

// ─────────────────────────────────────────────────────
// OBD2 ativo sobre o CAN (ROADMAP.md §6) — PIDs Mode 01 padrao (SAE
// J1979). So funciona com o modo OBD2 ligado (app_can_obd2_set_active),
// que reinstala o driver TWAI em NORMAL — o painel passa a TRANSMITIR
// requisicoes no barramento, nao so escutar. Ver aviso na propria aba.
//
// O round robin dos PIDs e a decodificacao das respostas moram no
// app_can (task de captura), nao aqui: assim o dado continua vivo com esta
// tela fechada e o dashboard (botao "CAN") consome exatamente a mesma
// fonte. Esta tela so LE o snapshot e desenha.
// ─────────────────────────────────────────────────────
typedef enum {
    OBD2_ROW_RPM = 0, OBD2_ROW_SPEED, OBD2_ROW_ECT,
    OBD2_ROW_IAT, OBD2_ROW_MAP, OBD2_ROW_TPS, OBD2_ROW_BATT,
    OBD2_ROW_COUNT,
} obd2_row_t;

typedef struct { const char *label; const char *unit; } obd2_row_def_t;

static const obd2_row_def_t k_obd2_rows[OBD2_ROW_COUNT] = {
    [OBD2_ROW_RPM]   = { "RPM",           ""     },
    [OBD2_ROW_SPEED] = { "Velocidade",    "km/h" },
    [OBD2_ROW_ECT]   = { "Arrefecimento", "C"    },
    [OBD2_ROW_IAT]   = { "Ar admissao",   "C"    },
    [OBD2_ROW_MAP]   = { "MAP",           "kPa"  },
    [OBD2_ROW_TPS]   = { "TPS",           "%"    },
    [OBD2_ROW_BATT]  = { "Bateria",       "V"    },
};

// ── Estado da tela ────────────────────────────────────
static lv_obj_t   *s_lbl_can_status  = NULL;
static lv_obj_t   *s_lbl_obd2_status = NULL;
static lv_obj_t   *s_btn_obd2        = NULL;
static lv_obj_t   *s_lbl_obd2_val[OBD2_ROW_COUNT];
static lv_timer_t *s_poll_timer      = NULL;

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

static void clear_obd2_values(void)
{
    for (int i = 0; i < OBD2_ROW_COUNT; i++) {
        if (s_lbl_obd2_val[i]) lv_label_set_text(s_lbl_obd2_val[i], "---");
    }
}

static void set_obd2_button_appearance(bool active)
{
    if (!s_btn_obd2) return;
    lv_obj_t *lbl = lv_obj_get_child(s_btn_obd2, 0);
    if (lbl) {
        lv_label_set_text(lbl, active ? LV_SYMBOL_STOP " Desativar OBD2" : LV_SYMBOL_PLAY " Ativar OBD2");
        lv_obj_center(lbl);
    }
    lv_obj_set_style_bg_color(s_btn_obd2, active ? ZOTTI_RED : ZOTTI_GREEN, 0);
}

static void obd2_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    bool enable = !app_can_obd2_is_active();
    esp_err_t err = app_can_obd2_set_active(enable);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao %s OBD2 (err=%d)", enable ? "ativar" : "desativar", (int)err);
        return;
    }
    set_obd2_button_appearance(enable);
    if (!enable) clear_obd2_values();
}

static void poll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    app_can_status_t st;
    app_can_sniffer_get_status(&st);
    if (s_lbl_can_status) {
        lv_label_set_text(s_lbl_can_status, st.driver_ready ? "CAN: OK" : "CAN: OFF");
        lv_obj_set_style_text_color(s_lbl_can_status, st.driver_ready ? ZOTTI_GREEN : ZOTTI_RED, 0);
    }

    bool active = app_can_obd2_is_active();
    if (s_lbl_obd2_status) {
        lv_label_set_text(s_lbl_obd2_status, active
            ? LV_SYMBOL_WARNING "  OBD2 ATIVO — o painel esta TRANSMITINDO requisicoes no barramento"
            : "OBD2 desligado — o CAN so escuta (modo padrao, seguro)");
        lv_obj_set_style_text_color(s_lbl_obd2_status, active ? ZOTTI_YELLOW : ZOTTI_GRAY, 0);
    }
    if (!active) return;

    // Snapshot decodificado pelo app_can (quem pede os PIDs e decodifica as
    // respostas e a task de captura — ver ROADMAP.md §6).
    app_can_obd2_data_t d;
    app_can_obd2_get_data(&d);
    if (!d.valid) {
        // Sem resposta ha mais de APP_CAN_OBD2_STALE_MS: melhor mostrar "---"
        // do que congelar o ultimo valor lido como se ainda fosse atual.
        clear_obd2_values();
        return;
    }

    const float vals[OBD2_ROW_COUNT] = {
        [OBD2_ROW_RPM]   = (float)d.rpm,
        [OBD2_ROW_SPEED] = (float)d.speed_kph,
        [OBD2_ROW_ECT]   = (float)d.ect_c,
        [OBD2_ROW_IAT]   = (float)d.iat_c,
        [OBD2_ROW_MAP]   = (float)d.map_kpa,
        [OBD2_ROW_TPS]   = (float)d.tps_pct,
        [OBD2_ROW_BATT]  = d.batt_v,
    };

    for (int i = 0; i < OBD2_ROW_COUNT; i++) {
        if (!s_lbl_obd2_val[i]) continue;
        if (k_obd2_rows[i].unit[0]) {
            lv_label_set_text_fmt(s_lbl_obd2_val[i], "%.1f %s", (double)vals[i], k_obd2_rows[i].unit);
        } else {
            lv_label_set_text_fmt(s_lbl_obd2_val[i], "%.0f", (double)vals[i]);
        }
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_poll_timer) { lv_timer_delete(s_poll_timer); s_poll_timer = NULL; }
    s_lbl_can_status = s_lbl_obd2_status = s_btn_obd2 = NULL;
    for (int i = 0; i < OBD2_ROW_COUNT; i++) s_lbl_obd2_val[i] = NULL;
}

// ─────────────────────────────────────────────────────
// Abas internas do modulo CAN.
// ─────────────────────────────────────────────────────
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

    // Placeholder de mensagem — a captura de verdade, com tabela ao vivo,
    // esta na tela "CAN sniffer" (long-press no titulo do menu principal).
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Captura detalhada em CAN Sniffer\n(pressione e segure o titulo do menu principal).");
    lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 30);
}

// Aba Decoder: OBD2 ativo sobre o CAN — le sensores padrao SAE J1979 do
// Vectra via requisicao Mode 01 direta (nao usa ELM327/BLE, que fica na
// tela Scanner — protocolo ainda nao definido, ver ROADMAP.md §12).
static void build_decoder_tab(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 34);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, obd2_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl_btn, ZOTTI_FONT_TINY, 0);
    s_btn_obd2 = btn;

    s_lbl_obd2_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_obd2_status, "OBD2 desligado — o CAN so escuta (modo padrao, seguro)");
    lv_obj_set_style_text_font(s_lbl_obd2_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_obd2_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_obd2_status, LV_ALIGN_TOP_LEFT, 240, 8);

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 740, 190);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < OBD2_ROW_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, 178, 80);
        lv_obj_set_style_bg_color(card, ZOTTI_BG_CARD, 0);
        lv_obj_set_style_border_color(card, ZOTTI_BORDER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, k_obd2_rows[i].label);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 10, 8);

        lv_obj_t *lbl_val = lv_label_create(card);
        lv_label_set_text(lbl_val, "---");
        lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
        lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        s_lbl_obd2_val[i] = lbl_val;
    }

    set_obd2_button_appearance(app_can_obd2_is_active());
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
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

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

    // Status da interface CAN — reflete o driver TWAI de verdade (app_can).
    s_lbl_can_status = lv_label_create(header);
    lv_label_set_text(s_lbl_can_status, "CAN: OFF");
    lv_obj_set_style_text_font(s_lbl_can_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_can_status, ZOTTI_RED, 0);
    lv_obj_align(s_lbl_can_status, LV_ALIGN_RIGHT_MID, -10, 0);

    // Filtros (usados pela aba Sniffer/tela CAN Sniffer — ver ui_screen_can_sniffer.c).
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

    s_poll_timer = lv_timer_create(poll_timer_cb, 300, NULL);
    poll_timer_cb(NULL);

    ui_screen_load(scr);
    ESP_LOGI(TAG, "CAN criado");
}
