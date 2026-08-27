#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_bcu.h"
#include "app_ecu.h"
#include "app_sim.h"

#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "UI_ECU";

static void update_values(lv_timer_t *timer);

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

// ─────────────────────────────────────────────────────
// Fonte de dados desta tela — mesmo padrao de tres fontes mutuamente
// exclusivas do dashboard (ui_screen_dashboard.c, ROADMAP.md §10):
//   ECU  — padrao. Telemetria BLE da ECU programavel (app_ecu).
//   DEMO — simulador local (app_sim), sem hardware nenhum ligado.
//   CAN  — OBD2 Mode 01 sobre o CAN do carro de fabrica (app_can,
//          ROADMAP.md §6). ATENCAO: nesse modo o painel TRANSMITE
//          requisicoes no barramento (driver TWAI em NORMAL).
// ─────────────────────────────────────────────────────
typedef enum { ECU_SRC_ECU = 0, ECU_SRC_DEMO, ECU_SRC_CAN } ecu_src_t;

static ecu_src_t s_source   = ECU_SRC_ECU;
static lv_obj_t *s_btn_demo = NULL;
static lv_obj_t *s_btn_can  = NULL;

static void refresh_source_buttons(void)
{
    if (s_btn_demo) {
        lv_obj_set_style_bg_color(s_btn_demo,
            (s_source == ECU_SRC_DEMO) ? ZOTTI_YELLOW : ZOTTI_BG_CARD, 0);
    }
    if (s_btn_can) {
        // Vermelho, nao verde: o modo CAN transmite no barramento do carro.
        lv_obj_set_style_bg_color(s_btn_can,
            (s_source == ECU_SRC_CAN) ? ZOTTI_RED : ZOTTI_BG_CARD, 0);
    }
}

static void set_source(ecu_src_t src)
{
    if (src == s_source) return;

    // Sair do modo CAN devolve o TWAI pro LISTEN_ONLY — o painel volta a so
    // escutar assim que a fonte deixa de ser o OBD2. Aviso: reinstala o
    // driver e espera 100ms com a task do LVGL parada, mesmo custo que o
    // botao da tela do CAN/dashboard ja paga; aceitavel num toque deliberado.
    if (s_source == ECU_SRC_CAN && app_bcu_obd2_is_active()) {
        app_bcu_obd2_set_active(false);
    }

    if (src == ECU_SRC_CAN && !app_bcu_obd2_is_active()) {
        esp_err_t err = app_bcu_obd2_set_active(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Nao foi possivel ativar o OBD2 (err=%d) — voltando pra ECU", (int)err);
            src = ECU_SRC_ECU;
        }
    }

    app_sim_set_enabled(src == ECU_SRC_DEMO);

    s_source = src;
    refresh_source_buttons();
    update_values(NULL);
}

static void demo_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    set_source(s_source == ECU_SRC_DEMO ? ECU_SRC_ECU : ECU_SRC_DEMO);
}

static void can_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    set_source(s_source == ECU_SRC_CAN ? ECU_SRC_ECU : ECU_SRC_CAN);
}

// Sensores recebidos via BLE da ECU externa.
typedef struct {
    const char *name;
    const char *unit;
    lv_color_t  color;
} ecu_sensor_t;

// Indices em k_sensors — usados por update_values() para saber qual label
// de valor corresponde a qual dado do app_ecu_data_t.
enum {
    SENS_RPM = 0,
    SENS_MAP,
    SENS_TPS,
    SENS_AFR,
    SENS_LAMBDA,
    SENS_ECT,
    SENS_IAT,
    SENS_PRESSAO,   // sem fonte de dado ainda — fica sempre "---"
    SENS_BATERIA,
    SENS_ESTADO,
    SENS_COUNT
};

static const ecu_sensor_t k_sensors[SENS_COUNT] = {
    { "RPM",      "rpm",  {0} },
    { "MAP",      "kPa",  {0} },
    { "TPS",      "%",    {0} },
    { "AFR",      "",     {0} },
    { "Lambda",   "",     {0} },
    { "ECT",      "C",    {0} },
    { "IAT",      "C",    {0} },
    { "Pressao",  "kPa",  {0} },
    { "Bateria",  "V",    {0} },
    { "Estado",   "",     {0} },
};

// ─────────────────────────────────────────────────────
// Estado da tela (para o timer de atualização)
// ─────────────────────────────────────────────────────
static lv_obj_t   *s_lbl_val[SENS_COUNT] = {0};
static lv_obj_t   *s_lbl_ble             = NULL;
static lv_timer_t *s_timer               = NULL;

static void set_val_text(int idx, const char *fmt, ...)
{
    if (!s_lbl_val[idx]) return;
    char buf[16];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(s_lbl_val[idx], buf);
}

// Chamado a cada 300ms pelo s_timer — só lê o snapshot já pronto do
// app_ecu (mutex + memcpy, rápido) e formata texto. Nenhum I/O aqui.
static void update_values(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_source == ECU_SRC_DEMO) {
        app_sim_data_t d;
        app_sim_get_data(&d);
        if (s_lbl_ble) {
            lv_label_set_text(s_lbl_ble, LV_SYMBOL_PLAY "  SIMULADOR LOCAL ATIVO (sem BLE)");
            lv_obj_set_style_text_color(s_lbl_ble, ZOTTI_YELLOW, 0);
        }
        set_val_text(SENS_ESTADO, "DEMO");
        set_val_text(SENS_RPM,     "%u rpm", (unsigned)d.rpm);
        set_val_text(SENS_MAP,     "%u kPa", (unsigned)d.map_kpa);
        set_val_text(SENS_TPS,     "%u %%", (unsigned)d.tps_pct);
        set_val_text(SENS_LAMBDA,  "%.3f", (double)d.lambda);
        set_val_text(SENS_AFR,     "%.1f", (double)(d.lambda * 14.7f));
        set_val_text(SENS_ECT,     "%d °C", (int)d.ect_c);
        set_val_text(SENS_IAT,     "%d °C", (int)d.iat_c);
        set_val_text(SENS_PRESSAO, "---");
        set_val_text(SENS_BATERIA, "%.1f V", (double)d.batt_v);
        return;
    }

    if (s_source == ECU_SRC_CAN) {
        app_bcu_obd2_data_t d;
        app_bcu_obd2_get_data(&d);
        if (s_lbl_ble) {
            lv_label_set_text(s_lbl_ble, d.valid
                ? LV_SYMBOL_WARNING "  CAN/OBD2 lendo (transmitindo)"
                : LV_SYMBOL_WARNING "  CAN/OBD2 sem resposta");
            lv_obj_set_style_text_color(s_lbl_ble, d.valid ? ZOTTI_GREEN : ZOTTI_RED, 0);
        }
        if (!d.valid) {
            set_val_text(SENS_ESTADO, "---");
            for (int i = 0; i < SENS_COUNT; i++) {
                if (i != SENS_ESTADO) set_val_text(i, "---");
            }
            return;
        }
        // Mode 01 padrao (ROADMAP.md §6) nao traz lambda/AFR nem pressao —
        // ficam "---" de proposito, igual ao dashboard no modo CAN.
        set_val_text(SENS_ESTADO,  "CAN");
        set_val_text(SENS_RPM,     "%u rpm", (unsigned)d.rpm);
        set_val_text(SENS_MAP,     "%u kPa", (unsigned)d.map_kpa);
        set_val_text(SENS_TPS,     "%u %%", (unsigned)d.tps_pct);
        set_val_text(SENS_LAMBDA,  "---");
        set_val_text(SENS_AFR,     "---");
        set_val_text(SENS_ECT,     "%d °C", (int)d.ect_c);
        set_val_text(SENS_IAT,     "%d °C", (int)d.iat_c);
        set_val_text(SENS_PRESSAO, "---");
        set_val_text(SENS_BATERIA, "%.1f V", (double)d.batt_v);
        return;
    }

    app_ecu_status_t st;
    app_ecu_get_status(&st);

    if (s_lbl_ble) {
        bool connected = (st.state == APP_ECU_STATE_CONNECTED);
        lv_label_set_text(s_lbl_ble, connected
            ? LV_SYMBOL_BLUETOOTH "  ECU BLE: CONECTADA"
            : LV_SYMBOL_BLUETOOTH "  ECU BLE: DESCONECTADA");
        lv_obj_set_style_text_color(s_lbl_ble, connected ? ZOTTI_GREEN : ZOTTI_RED, 0);
    }
    set_val_text(SENS_ESTADO, "%s",
        st.state == APP_ECU_STATE_CONNECTED  ? "OK" :
        st.state == APP_ECU_STATE_CONNECTING ? "..." : "---");

    app_ecu_data_t d;
    app_ecu_get_data(&d);
    if (!d.valid) {
        for (int i = 0; i < SENS_COUNT; i++) {
            set_val_text(i, "---");
        }
        return;
    }

    set_val_text(SENS_RPM,    "%u rpm",   (unsigned)d.rpm);
    set_val_text(SENS_MAP,    "%u kPa",   (unsigned)d.map_kpa);
    set_val_text(SENS_TPS,    "%u %%",   (unsigned)d.tps_pct);
    set_val_text(SENS_LAMBDA, "%.3f", (double)d.lambda);
    set_val_text(SENS_AFR,    "%.1f", (double)(d.lambda * 14.7f));  // gasolina, estequiometrico=14.7
    set_val_text(SENS_ECT,    "%d °C",   (int)d.ect_c);
    set_val_text(SENS_IAT,    "%d °C",   (int)d.iat_c);
    set_val_text(SENS_BATERIA,"%.1f V", (double)d.batt_v);
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    for (int i = 0; i < SENS_COUNT; i++) s_lbl_val[i] = NULL;
    s_lbl_ble = NULL;
    s_btn_demo = s_btn_can = NULL;
}

void ui_screen_ecu_show(void)
{
    // Reconcilia a fonte com o estado real dos componentes — s_source e uma
    // preferencia desta tela, mas app_can/app_sim tambem sao ligados por
    // OUTRAS telas (botao OBD2 da tela CAN, botao CAN/Demo do dashboard).
    // Mesmo criterio do dashboard: OBD2 no ar e a fonte mais "cara", ganha.
    if (app_bcu_obd2_is_active()) {
        s_source = ECU_SRC_CAN;
    } else if (s_source == ECU_SRC_CAN) {
        s_source = ECU_SRC_ECU;
    } else {
        s_source = app_sim_is_enabled() ? ECU_SRC_DEMO : ECU_SRC_ECU;
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
    lv_label_set_text(lbl_title, "MONITOR ECU BLE");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_btn_demo = lv_btn_create(header);
    lv_obj_set_size(s_btn_demo, 110, 28);
    lv_obj_align(s_btn_demo, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_radius(s_btn_demo, 4, 0);
    lv_obj_add_event_cb(s_btn_demo, demo_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_demo = lv_label_create(s_btn_demo);
    lv_label_set_text(lbl_demo, LV_SYMBOL_PLAY " Demo");
    lv_obj_set_style_text_font(lbl_demo, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_demo);

    s_btn_can = lv_btn_create(header);
    lv_obj_set_size(s_btn_can, 110, 28);
    lv_obj_align(s_btn_can, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_set_style_radius(s_btn_can, 4, 0);
    lv_obj_add_event_cb(s_btn_can, can_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_can = lv_label_create(s_btn_can);
    lv_label_set_text(lbl_can, LV_SYMBOL_CHARGE " CAN");
    lv_obj_set_style_text_font(lbl_can, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_can);

    refresh_source_buttons();

    // Status conexao BLE.
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 36);
    lv_obj_set_pos(status_bar, 0, 40);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_ble = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_ble, LV_SYMBOL_BLUETOOTH "  ECU BLE: DESCONECTADA");
    lv_obj_set_style_text_font(s_lbl_ble, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_ble, ZOTTI_RED, 0);
    lv_obj_align(s_lbl_ble, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *lbl_proto = lv_label_create(status_bar);
    lv_label_set_text(lbl_proto, "Comando: WB1 via BLE para ativar");
    lv_obj_set_style_text_font(lbl_proto, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_proto, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_proto, LV_ALIGN_RIGHT_MID, -15, 0);

    // Grade de sensores.
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
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, k_sensors[i].name);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 14, 8);

        lv_obj_t *lbl_val = lv_label_create(card);
        lv_label_set_text(lbl_val, "---");
        lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_LARGE, 0);
        lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
        lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
        s_lbl_val[i] = lbl_val;

        lv_obj_t *lbl_unit = lv_label_create(card);
        lv_label_set_text(lbl_unit, k_sensors[i].unit);
        lv_obj_set_style_text_font(lbl_unit, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_unit, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        lv_obj_add_flag(lbl_unit, LV_OBJ_FLAG_HIDDEN);
    }

    s_timer = lv_timer_create(update_values, 100, NULL);
    update_values(NULL);   // primeiro frame já com o estado atual, sem esperar 300ms

    ui_screen_load(scr);
    ESP_LOGI(TAG, "ECU criado");
}
