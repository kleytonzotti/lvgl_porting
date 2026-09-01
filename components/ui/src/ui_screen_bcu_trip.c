#include "ui.h"
#include "app_bcu.h"
#include "app_sim.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_BCU_TRIP";

// ─────────────────────────────────────────────────────
// COMPUTADOR DE BORDO — layout no estilo dos painéis "top de mercado"
// (velocidade grande em destaque + cartões de viagem: distância, média,
// máxima, tempo — com Zerar e um indicador de Cruise Control).
//
// Fonte de dados: igual ao padrão de ui_screen_ecu.c/ui_screen_dashboard.c,
// mas só com duas opções (CAN/Demo) em vez de três — o protocolo BLE da
// ECU programável (app_ecu, ROADMAP.md §4) não carrega velocidade nenhuma,
// então não haveria o que mostrar aqui nessa fonte. CAN é o padrão por ser
// o dado real do carro (app_bcu, ROADMAP.md §6); Demo é só pra
// demonstração sem hardware.
//
// Cruise Control aqui é só um INDICADOR local, nunca um comando: o painel
// não tem (e não deve ter) characteristic de escrita pra ECU nem qualquer
// outro jeito de atuar no carro — ver a "regra de ouro" do ROADMAP.md §1.
// ─────────────────────────────────────────────────────

typedef enum { TRIP_SRC_NONE = 0, TRIP_SRC_CAN, TRIP_SRC_DEMO } trip_src_t;

static trip_src_t s_source = TRIP_SRC_NONE;

// ── Widgets ───────────────────────────────────────────
static lv_obj_t *s_lbl_status     = NULL;
static lv_obj_t *s_btn_can        = NULL;
static lv_obj_t *s_btn_demo       = NULL;
static lv_obj_t *s_lbl_speed      = NULL;
static lv_obj_t *s_lbl_rpm        = NULL;
static lv_obj_t *s_lbl_ect        = NULL;
static lv_obj_t *s_lbl_batt       = NULL;
static lv_obj_t *s_lbl_dist       = NULL;
static lv_obj_t *s_lbl_avg        = NULL;
static lv_obj_t *s_lbl_max        = NULL;
static lv_obj_t *s_lbl_time       = NULL;
static lv_obj_t *s_btn_cruise     = NULL;
static lv_obj_t *s_lbl_cruise_val = NULL;
static lv_timer_t *s_timer        = NULL;

// ── Acumuladores de viagem — sobrevivem à troca de tela (só "Zerar" zera) ──
static float    s_trip_km       = 0.0f;
static float    s_max_speed_kph = 0.0f;
static uint32_t s_trip_start_tk = 0;
static uint32_t s_last_tk       = 0;
static bool     s_have_last     = false;
static bool     s_trip_started  = false;

// Indicador de Cruise Control — só estado visual, nunca enviado a lugar
// nenhum (ver aviso no topo do arquivo).
static bool s_cruise_on = false;

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

static void refresh_source_buttons(void)
{
    if (s_btn_can) {
        // Vermelho: modo CAN aqui liga o OBD2 ativo, que TRANSMITE no
        // barramento do carro (mesma convenção das outras telas).
        lv_obj_set_style_bg_color(s_btn_can,
            (s_source == TRIP_SRC_CAN) ? ZOTTI_RED : ZOTTI_BG_CARD, 0);
    }
    if (s_btn_demo) {
        lv_obj_set_style_bg_color(s_btn_demo,
            (s_source == TRIP_SRC_DEMO) ? ZOTTI_YELLOW : ZOTTI_BG_CARD, 0);
    }
}

static void set_source(trip_src_t src)
{
    if (src == s_source) return;

    if (s_source == TRIP_SRC_CAN && app_bcu_obd2_is_active()) {
        app_bcu_obd2_set_active(false);
    }
    if (src == TRIP_SRC_CAN && !app_bcu_obd2_is_active()) {
        esp_err_t err = app_bcu_obd2_set_active(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Nao foi possivel ativar o OBD2 (err=%d)", (int)err);
            src = TRIP_SRC_NONE;
        }
    }

    app_sim_set_enabled(src == TRIP_SRC_DEMO);

    s_source   = src;
    s_have_last = false;   // evita salto de distancia com o dt acumulado da fonte antiga
    refresh_source_buttons();
}

static void can_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    set_source(s_source == TRIP_SRC_CAN ? TRIP_SRC_NONE : TRIP_SRC_CAN);
}

static void demo_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    set_source(s_source == TRIP_SRC_DEMO ? TRIP_SRC_NONE : TRIP_SRC_DEMO);
}

static void zerar_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_trip_km       = 0.0f;
    s_max_speed_kph = 0.0f;
    s_trip_start_tk = lv_tick_get();
    s_have_last     = false;
    s_trip_started  = true;
    if (s_lbl_dist) lv_label_set_text(s_lbl_dist, "0.0 km");
    if (s_lbl_avg)  lv_label_set_text(s_lbl_avg,  "0 km/h");
    if (s_lbl_max)  lv_label_set_text(s_lbl_max,  "0 km/h");
    if (s_lbl_time) lv_label_set_text(s_lbl_time, "00:00:00");
}

static void cruise_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_cruise_on = !s_cruise_on;
    if (s_btn_cruise) {
        lv_obj_set_style_bg_color(s_btn_cruise, s_cruise_on ? ZOTTI_GREEN : ZOTTI_BG_CARD, 0);
    }
    if (s_lbl_cruise_val) {
        lv_label_set_text(s_lbl_cruise_val, s_cruise_on ? "CRUISE: ON (indicativo)" : "CRUISE: OFF");
        lv_obj_set_style_text_color(s_lbl_cruise_val, s_cruise_on ? ZOTTI_GREEN : ZOTTI_GRAY, 0);
    }
}

static void format_hms(char *buf, size_t len, uint32_t elapsed_ms)
{
    uint32_t s = elapsed_ms / 1000u;
    uint32_t h = s / 3600u;
    uint32_t m = (s % 3600u) / 60u;
    uint32_t ss = s % 60u;
    snprintf(buf, len, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)ss);
}

// Chamado a cada 250ms — le o snapshot da fonte ativa, integra distancia
// (velocidade x dt) e atualiza os cartoes de viagem.
static void update_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (!s_trip_started) {
        s_trip_start_tk = lv_tick_get();
        s_trip_started  = true;
    }

    bool  valid     = false;
    int32_t speed_i = 0, rpm_i = 0, ect_i = 0;
    float batt      = 0.0f;

    if (s_source == TRIP_SRC_CAN) {
        app_bcu_obd2_data_t d;
        app_bcu_obd2_get_data(&d);
        valid   = d.valid;
        speed_i = d.speed_kph;
        rpm_i   = d.rpm;
        ect_i   = d.ect_c;
        batt    = d.batt_v;
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, d.valid
                ? LV_SYMBOL_WARNING "  CAN/OBD2 lendo (transmitindo)"
                : LV_SYMBOL_WARNING "  CAN/OBD2 sem resposta");
            lv_obj_set_style_text_color(s_lbl_status, d.valid ? ZOTTI_GREEN : ZOTTI_RED, 0);
        }
    } else if (s_source == TRIP_SRC_DEMO) {
        app_sim_data_t d;
        app_sim_get_data(&d);
        valid   = true;
        speed_i = d.speed_kph;
        rpm_i   = d.rpm;
        ect_i   = d.ect_c;
        batt    = d.batt_v;
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, LV_SYMBOL_PLAY "  SIMULADOR LOCAL ATIVO");
            lv_obj_set_style_text_color(s_lbl_status, ZOTTI_YELLOW, 0);
        }
    } else {
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, "Selecione uma fonte de dados (CAN ou Demo)");
            lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
        }
    }

    uint32_t now_tk = lv_tick_get();

    if (valid) {
        if (s_have_last) {
            float dt_h = (float)(now_tk - s_last_tk) / 3600000.0f;
            s_trip_km += (float)speed_i * dt_h;
        }
        s_have_last = true;
        s_last_tk   = now_tk;
        if ((float)speed_i > s_max_speed_kph) s_max_speed_kph = (float)speed_i;
    } else {
        s_have_last = false;   // sem dado: nao integra um dt "parado" quando voltar
    }

    if (s_lbl_speed) lv_label_set_text_fmt(s_lbl_speed, valid ? "%d" : "---", (int)speed_i);
    if (s_lbl_rpm)   lv_label_set_text_fmt(s_lbl_rpm,   valid ? "%d rpm"  : "---", (int)rpm_i);
    if (s_lbl_ect)   lv_label_set_text_fmt(s_lbl_ect,   valid ? "%d °C"   : "---", (int)ect_i);
    if (s_lbl_batt)  lv_label_set_text_fmt(s_lbl_batt,  valid ? "%.1f V"  : "---", (double)batt);

    uint32_t elapsed_ms = now_tk - s_trip_start_tk;
    float    elapsed_h  = (float)elapsed_ms / 3600000.0f;
    float    avg_speed  = (elapsed_h > 0.0f) ? (s_trip_km / elapsed_h) : 0.0f;

    if (s_lbl_dist) lv_label_set_text_fmt(s_lbl_dist, "%.1f km", (double)s_trip_km);
    if (s_lbl_avg)  lv_label_set_text_fmt(s_lbl_avg,  "%.0f km/h", (double)avg_speed);
    if (s_lbl_max)  lv_label_set_text_fmt(s_lbl_max,  "%.0f km/h", (double)s_max_speed_kph);
    if (s_lbl_time) {
        char buf[16];
        format_hms(buf, sizeof(buf), elapsed_ms);
        lv_label_set_text(s_lbl_time, buf);
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_lbl_status = s_btn_can = s_btn_demo = NULL;
    s_lbl_speed = s_lbl_rpm = s_lbl_ect = s_lbl_batt = NULL;
    s_lbl_dist = s_lbl_avg = s_lbl_max = s_lbl_time = NULL;
    s_btn_cruise = s_lbl_cruise_val = NULL;
}

// Cria um cartao pequeno "titulo em cima / valor grande embaixo", mesmo
// padrao visual usado em ui_screen_ecu.c.
static lv_obj_t *create_card(lv_obj_t *parent, int32_t w, int32_t h,
                              const char *title, lv_font_t const *value_font)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(card, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 12, 8);

    lv_obj_t *lbl_val = lv_label_create(card);
    lv_label_set_text(lbl_val, "---");
    lv_obj_set_style_text_font(lbl_val, value_font, 0);
    lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    return lbl_val;
}

void ui_screen_bcu_trip_show(void)
{
    // Reconcilia a fonte com o estado real dos componentes — mesmo criterio
    // das outras telas (ui_screen_ecu.c/ui_screen_dashboard.c): OBD2 no ar
    // ganha; senao segue o simulador; senao fica sem fonte selecionada.
    if (app_bcu_obd2_is_active()) {
        s_source = TRIP_SRC_CAN;
    } else if (s_source == TRIP_SRC_CAN) {
        s_source = TRIP_SRC_NONE;
    } else {
        s_source = app_sim_is_enabled() ? TRIP_SRC_DEMO : s_source;
    }

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
    lv_label_set_text(lbl_title, "COMPUTADOR DE BORDO");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_btn_demo = lv_btn_create(header);
    lv_obj_set_size(s_btn_demo, 90, 28);
    lv_obj_align(s_btn_demo, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_radius(s_btn_demo, 4, 0);
    lv_obj_add_event_cb(s_btn_demo, demo_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_demo = lv_label_create(s_btn_demo);
    lv_label_set_text(lbl_demo, LV_SYMBOL_PLAY " Demo");
    lv_obj_set_style_text_font(lbl_demo, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_demo);

    s_btn_can = lv_btn_create(header);
    lv_obj_set_size(s_btn_can, 90, 28);
    lv_obj_align(s_btn_can, LV_ALIGN_RIGHT_MID, -100, 0);
    lv_obj_set_style_radius(s_btn_can, 4, 0);
    lv_obj_add_event_cb(s_btn_can, can_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_can = lv_label_create(s_btn_can);
    lv_label_set_text(lbl_can, LV_SYMBOL_GPS " CAN");
    lv_obj_set_style_text_font(lbl_can, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_can);

    refresh_source_buttons();

    // Barra de status da fonte.
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 32);
    lv_obj_set_pos(status_bar, 0, 40);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_status, "Selecione uma fonte de dados (CAN ou Demo)");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 15, 0);

    // ── Linha principal: velocidade em destaque + RPM/Temp/Bateria ──
    lv_obj_t *main_row = lv_obj_create(scr);
    lv_obj_set_size(main_row, 800, 140);
    lv_obj_set_pos(main_row, 0, 72);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    lv_obj_set_style_pad_all(main_row, 10, 0);
    lv_obj_set_style_pad_column(main_row, 10, 0);
    lv_obj_set_layout(main_row, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(main_row, LV_FLEX_FLOW_ROW, 0);
    lv_obj_clear_flag(main_row, LV_OBJ_FLAG_SCROLLABLE);

    // Cartao de velocidade — maior e acentuado, e o dado mais importante
    // da tela (mesmo lugar de destaque que os painéis "top de mercado"
    // reservam pro velocimetro digital).
    lv_obj_t *card_speed = lv_obj_create(main_row);
    lv_obj_set_size(card_speed, 260, 120);
    lv_obj_set_style_bg_color(card_speed, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(card_speed, ZOTTI_ACCENT, 0);
    lv_obj_set_style_border_width(card_speed, 2, 0);
    lv_obj_set_style_radius(card_speed, 14, 0);
    lv_obj_clear_flag(card_speed, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_speed = lv_label_create(card_speed);
    lv_label_set_text(s_lbl_speed, "---");
    lv_obj_set_style_text_font(s_lbl_speed, ZOTTI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_lbl_speed, ZOTTI_ACCENT, 0);
    lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *lbl_kph_unit = lv_label_create(card_speed);
    lv_label_set_text(lbl_kph_unit, "km/h");
    lv_obj_set_style_text_font(lbl_kph_unit, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_kph_unit, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_kph_unit, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_lbl_rpm  = create_card(main_row, 170, 120, "RPM",      ZOTTI_FONT_MEDIUM);
    s_lbl_ect  = create_card(main_row, 170, 120, "Temp. Motor", ZOTTI_FONT_MEDIUM);
    s_lbl_batt = create_card(main_row, 170, 120, "Bateria",  ZOTTI_FONT_MEDIUM);

    // ── Cartoes de viagem: Distancia / Vel. Media / Vel. Maxima / Tempo ──
    lv_obj_t *trip_row = lv_obj_create(scr);
    lv_obj_set_size(trip_row, 800, 130);
    lv_obj_set_pos(trip_row, 0, 216);
    lv_obj_set_style_bg_opa(trip_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trip_row, 0, 0);
    lv_obj_set_style_pad_all(trip_row, 10, 0);
    lv_obj_set_style_pad_column(trip_row, 10, 0);
    lv_obj_set_layout(trip_row, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(trip_row, LV_FLEX_FLOW_ROW, 0);
    lv_obj_clear_flag(trip_row, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_dist = create_card(trip_row, 190, 110, "Distancia",     ZOTTI_FONT_MEDIUM);
    s_lbl_avg  = create_card(trip_row, 190, 110, "Vel. Media",    ZOTTI_FONT_MEDIUM);
    s_lbl_max  = create_card(trip_row, 190, 110, "Vel. Maxima",   ZOTTI_FONT_MEDIUM);
    s_lbl_time = create_card(trip_row, 190, 110, "Tempo",         ZOTTI_FONT_MEDIUM);

    // ── Barra de controle: Zerar + Cruise Control (indicativo) ──
    lv_obj_t *ctrl_bar = lv_obj_create(scr);
    lv_obj_set_size(ctrl_bar, 800, 100);
    lv_obj_set_pos(ctrl_bar, 0, 356);
    lv_obj_set_style_bg_opa(ctrl_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_bar, 0, 0);
    lv_obj_clear_flag(ctrl_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_zerar = lv_btn_create(ctrl_bar);
    lv_obj_set_size(btn_zerar, 180, 44);
    lv_obj_align(btn_zerar, LV_ALIGN_TOP_LEFT, 20, 0);
    lv_obj_set_style_bg_color(btn_zerar, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_zerar, 6, 0);
    lv_obj_add_event_cb(btn_zerar, zerar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_zerar = lv_label_create(btn_zerar);
    lv_label_set_text(lbl_zerar, LV_SYMBOL_REFRESH " Zerar viagem");
    lv_obj_set_style_text_font(lbl_zerar, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_zerar);

    s_btn_cruise = lv_btn_create(ctrl_bar);
    lv_obj_set_size(s_btn_cruise, 220, 44);
    lv_obj_align(s_btn_cruise, LV_ALIGN_TOP_LEFT, 220, 0);
    lv_obj_set_style_bg_color(s_btn_cruise, s_cruise_on ? ZOTTI_GREEN : ZOTTI_BG_CARD, 0);
    lv_obj_set_style_radius(s_btn_cruise, 6, 0);
    lv_obj_add_event_cb(s_btn_cruise, cruise_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cruise_btn = lv_label_create(s_btn_cruise);
    lv_label_set_text(lbl_cruise_btn, LV_SYMBOL_LOOP " Cruise Control");
    lv_obj_set_style_text_font(lbl_cruise_btn, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_cruise_btn);

    s_lbl_cruise_val = lv_label_create(ctrl_bar);
    lv_label_set_text(s_lbl_cruise_val, s_cruise_on ? "CRUISE: ON (indicativo)" : "CRUISE: OFF");
    lv_obj_set_style_text_font(s_lbl_cruise_val, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_cruise_val, s_cruise_on ? ZOTTI_GREEN : ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_cruise_val, LV_ALIGN_TOP_LEFT, 460, 14);

    lv_obj_t *lbl_note = lv_label_create(ctrl_bar);
    lv_label_set_text(lbl_note,
        "Cruise Control aqui e so um indicador — o painel nunca envia\n"
        "comando de controle para a ECU (ROADMAP.md, regra de ouro).");
    lv_obj_set_style_text_font(lbl_note, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_note, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_note, LV_ALIGN_TOP_LEFT, 20, 56);

    s_timer = lv_timer_create(update_timer_cb, 250, NULL);
    update_timer_cb(NULL);

    ui_screen_load(scr);
    ESP_LOGI(TAG, "Computador de Bordo criado");
}
