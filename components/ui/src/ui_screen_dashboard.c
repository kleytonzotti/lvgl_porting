#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_ecu.h"
#include "app_sim.h"
#include "app_dash_profile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_DASHBOARD";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Arco do RPM: mesmo range angular usado em lv_arc_set_bg_angles(135, 45)
// abaixo — 270 graus de varredura, abertura de 90 graus embaixo (visual de
// mostrador automotivo clássico). Usado tanto pra desenhar os tiquinhos do
// modo analógico quanto pra qualquer cálculo de ângulo futuro.
#define RPM_ARC_START_DEG   135.0f
#define RPM_ARC_SWEEP_DEG   270.0f
#define RPM_TICK_COUNT      6

// Referencias de widgets para atualizacao futura.
static lv_obj_t *s_arc_rpm     = NULL;
static lv_obj_t *s_lbl_rpm     = NULL;
static lv_obj_t *s_lbl_speed   = NULL;
static lv_obj_t *s_bar_tps     = NULL;
static lv_obj_t *s_lbl_map     = NULL;
static lv_obj_t *s_lbl_tps     = NULL;
static lv_obj_t *s_lbl_afr     = NULL;
static lv_obj_t *s_lbl_ect     = NULL;
static lv_obj_t *s_lbl_iat     = NULL;
static lv_obj_t *s_lbl_batt    = NULL;
static lv_obj_t *s_lbl_status  = NULL;
static lv_obj_t *s_ticks[RPM_TICK_COUNT];

static lv_obj_t   *s_scr           = NULL;
static lv_timer_t *s_timer         = NULL;
static bool        s_redline_flash = false;

static app_dash_profile_t s_active_profile;
static int32_t             s_active_index = 0;

// Modal de perfis — só existem enquanto o modal estiver aberto.
static lv_obj_t *s_modal_overlay     = NULL;
static lv_obj_t *s_modal_list        = NULL;
static lv_obj_t *s_modal_lbl_style   = NULL;
static lv_obj_t *s_modal_lbl_redline = NULL;

// ─────────────────────────────────────────────────────
// Estilo do mostrador (digital vs analógico)
// ─────────────────────────────────────────────────────

static void apply_gauge_style(app_dash_gauge_style_t style)
{
    bool analog = (style == APP_DASH_GAUGE_ANALOG);

    for (int i = 0; i < RPM_TICK_COUNT; i++) {
        if (!s_ticks[i]) continue;
        if (analog) lv_obj_clear_flag(s_ticks[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_ticks[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_arc_rpm) {
        // Modo analógico: anel mais fino + "ponteiro" (o knob do arco,
        // que o LVGL já posiciona sozinho na ponta do valor atual).
        lv_obj_set_style_arc_width(s_arc_rpm, analog ? 6 : 14, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_arc_rpm, analog ? 6 : 14, LV_PART_INDICATOR);
        lv_obj_set_style_opa(s_arc_rpm, analog ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_bg_color(s_arc_rpm, ZOTTI_WHITE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(s_arc_rpm, analog ? 3 : 0, LV_PART_KNOB);
    }
    if (s_lbl_rpm) {
        lv_obj_set_style_text_font(s_lbl_rpm, analog ? ZOTTI_FONT_LARGE : ZOTTI_FONT_HUGE, 0);
        lv_obj_align(s_lbl_rpm, LV_ALIGN_CENTER, 0, analog ? 15 : -30);
    }
}

// ─────────────────────────────────────────────────────
// Efeito de "perto do corte" — pisca o arco/numero entre a cor normal e
// vermelho enquanto o RPM estiver acima de 90% do redline do perfil ativo.
// ─────────────────────────────────────────────────────

static void redline_anim_exec_cb(void *var, int32_t v)
{
    lv_obj_t *arc = (lv_obj_t *)var;
    lv_color_t c = lv_color_mix(ZOTTI_RED, ZOTTI_WHITE, (uint8_t)v);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    if (s_lbl_rpm) lv_obj_set_style_text_color(s_lbl_rpm, c, 0);
}

static void redline_start(void)
{
    if (s_redline_flash || !s_arc_rpm) return;
    s_redline_flash = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arc_rpm);
    lv_anim_set_exec_cb(&a, redline_anim_exec_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 150);
    lv_anim_set_playback_time(&a, 150);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void redline_stop(void)
{
    if (!s_redline_flash) return;
    s_redline_flash = false;
    if (s_arc_rpm) {
        lv_anim_delete(s_arc_rpm, redline_anim_exec_cb);
        lv_obj_set_style_arc_color(s_arc_rpm, ZOTTI_ACCENT, LV_PART_INDICATOR);
    }
    if (s_lbl_rpm) lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
}

// ─────────────────────────────────────────────────────
// Animação suave do ponteiro/arco de RPM (em vez de saltar direto pro
// valor novo a cada atualização).
// ─────────────────────────────────────────────────────

static void arc_anim_exec_cb(void *var, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)var, v);
}

static void animate_arc_to(lv_obj_t *arc, int32_t value)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec_cb);
    lv_anim_set_values(&a, lv_arc_get_value(arc), value);
    lv_anim_set_time(&a, 180);
    lv_anim_start(&a);
}

// Atualiza os dados da dashboard.
void ui_screen_dashboard_update(int32_t rpm, int32_t speed_kph,
                                 int32_t map_kpa, int32_t tps_pct,
                                 float afr, int32_t ect_c,
                                 int32_t iat_c, float batt_v)
{
    if (!s_arc_rpm) return;

    animate_arc_to(s_arc_rpm, rpm);
    lv_label_set_text_fmt(s_lbl_rpm,   "%ld", (long)rpm);
    lv_label_set_text_fmt(s_lbl_speed, "%ld", (long)speed_kph);
    lv_bar_set_value(s_bar_tps, tps_pct, LV_ANIM_ON);
    lv_label_set_text_fmt(s_lbl_map,  "%ld kPa", (long)map_kpa);
    lv_label_set_text_fmt(s_lbl_tps,  "%ld%%", (long)tps_pct);
    if (s_lbl_afr)  lv_label_set_text_fmt(s_lbl_afr,  "%.1f", afr);
    if (s_lbl_ect)  lv_label_set_text_fmt(s_lbl_ect,  "%ld C", (long)ect_c);
    if (s_lbl_iat)  lv_label_set_text_fmt(s_lbl_iat,  "%ld C", (long)iat_c);
    lv_label_set_text_fmt(s_lbl_batt, "%.1fV", batt_v);

    // Cor do ECT: verde < 90 C, amarelo 90-105 C, vermelho > 105 C.
    if (s_lbl_ect) {
        lv_color_t ect_color = (ect_c < 90)  ? ZOTTI_GREEN  :
                               (ect_c < 105) ? ZOTTI_YELLOW : ZOTTI_RED;
        lv_obj_set_style_text_color(s_lbl_ect, ect_color, 0);
    }

    // Cor do AFR: verde se proximo de 14.7.
    if (s_lbl_afr) {
        lv_color_t afr_color = (afr > 14.0f && afr < 15.4f) ? ZOTTI_GREEN : ZOTTI_YELLOW;
        lv_obj_set_style_text_color(s_lbl_afr, afr_color, 0);
    }

    // Efeito de corte — 90% do redline do perfil ativo, com pequena
    // histerese (85%) pra nao ficar oscilando ligando/desligando na borda.
    if (rpm >= (int32_t)(s_active_profile.redline_rpm * 0.90f)) {
        redline_start();
    } else if (rpm < (int32_t)(s_active_profile.redline_rpm * 0.85f)) {
        redline_stop();
    }
}

// ─────────────────────────────────────────────────────
// Timer: escolhe a fonte de dados (demo ou ECU real) e atualiza a tela.
// ─────────────────────────────────────────────────────

static void update_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (app_sim_is_enabled()) {
        app_sim_data_t d;
        app_sim_get_data(&d);
        ui_screen_dashboard_update(d.rpm, d.speed_kph, d.map_kpa, d.tps_pct,
                                   d.lambda * 14.7f, d.ect_c, d.iat_c, d.batt_v);
        return;
    }

    app_ecu_status_t st;
    app_ecu_get_status(&st);
    if (s_lbl_status) {
        bool connected = (st.state == APP_ECU_STATE_CONNECTED);
        lv_label_set_text(s_lbl_status, connected
            ? LV_SYMBOL_BLUETOOTH " ECU: Conectada"
            : LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
        lv_obj_set_style_text_color(s_lbl_status, connected ? ZOTTI_GREEN : ZOTTI_GRAY, 0);
    }

    app_ecu_data_t d;
    app_ecu_get_data(&d);
    if (!d.valid) return;

    ui_screen_dashboard_update(d.rpm, 0, d.map_kpa, d.tps_pct,
                               d.lambda * 14.7f, d.ect_c, d.iat_c, d.batt_v);
}

// Card de sensor (lado direito).
static lv_obj_t *create_sensor_card(lv_obj_t *parent, const char *label_text,
                                     int32_t y_pos)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 215, 55);
    lv_obj_set_pos(card, 10, y_pos);
    lv_obj_set_style_bg_color(card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(card, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Label do sensor
    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, label_text);
    lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 8, -8);

    // Label do valor retornado para atualizacao.
    lv_obj_t *lbl_val = lv_label_create(card);
    lv_label_set_text(lbl_val, "---");
    lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_val, LV_ALIGN_RIGHT_MID, -8, 5);

    return lbl_val;
}

// ─────────────────────────────────────────────────────
// Modal de perfis
// ─────────────────────────────────────────────────────

static void persist_active_profile(void)
{
    app_dash_profile_save((uint32_t)s_active_index, &s_active_profile);
}

static void refresh_modal_editor_labels(void)
{
    if (s_modal_lbl_style) {
        lv_label_set_text(s_modal_lbl_style,
            s_active_profile.gauge_style == APP_DASH_GAUGE_ANALOG
                ? LV_SYMBOL_REFRESH "  Estilo: Analogico" : LV_SYMBOL_REFRESH "  Estilo: Digital");
    }
    if (s_modal_lbl_redline) {
        lv_label_set_text_fmt(s_modal_lbl_redline, "Corte: %u rpm", (unsigned)s_active_profile.redline_rpm);
    }
}

static void rebuild_profile_modal_list(void);   // fwd decl

static void profile_use_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    app_dash_profile_t p;
    if (app_dash_profile_get(idx, &p)) {
        s_active_profile = p;
        s_active_index = (int32_t)idx;
        app_dash_profile_set_active_index(idx);
        apply_gauge_style(s_active_profile.gauge_style);
        refresh_modal_editor_labels();
        rebuild_profile_modal_list();
    }
}

static void profile_delete_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    if (app_dash_profile_count() <= 1) return;   // nunca apaga o ultimo

    app_dash_profile_delete(idx);

    int32_t active = app_dash_profile_get_active_index();
    if (active < 0) active = 0;
    app_dash_profile_get((uint32_t)active, &s_active_profile);
    s_active_index = active;
    apply_gauge_style(s_active_profile.gauge_style);
    refresh_modal_editor_labels();
    rebuild_profile_modal_list();
}

static void profile_style_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_active_profile.gauge_style = (s_active_profile.gauge_style == APP_DASH_GAUGE_DIGITAL)
        ? APP_DASH_GAUGE_ANALOG : APP_DASH_GAUGE_DIGITAL;
    apply_gauge_style(s_active_profile.gauge_style);
    persist_active_profile();
    refresh_modal_editor_labels();
}

static void profile_redline_dec_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_active_profile.redline_rpm > 3250) s_active_profile.redline_rpm -= 250;
    persist_active_profile();
    refresh_modal_editor_labels();
}

static void profile_redline_inc_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_active_profile.redline_rpm < 12000) s_active_profile.redline_rpm += 250;
    persist_active_profile();
    refresh_modal_editor_labels();
}

static void profile_save_new_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t count = app_dash_profile_count();
    if (count >= APP_DASH_PROFILE_MAX) return;

    app_dash_profile_t novo = s_active_profile;
    snprintf(novo.name, sizeof(novo.name), "Perfil %u", (unsigned)(count + 1));
    if (!app_dash_profile_save(count, &novo)) return;

    app_dash_profile_set_active_index(count);
    s_active_profile = novo;
    s_active_index = (int32_t)count;
    rebuild_profile_modal_list();
}

static void profile_modal_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_modal_overlay) lv_obj_delete(s_modal_overlay);
    s_modal_overlay = s_modal_list = s_modal_lbl_style = s_modal_lbl_redline = NULL;
}

static void rebuild_profile_modal_list(void)
{
    if (!s_modal_list) return;
    lv_obj_clean(s_modal_list);

    uint32_t count = app_dash_profile_count();
    for (uint32_t i = 0; i < count; i++) {
        app_dash_profile_t p;
        if (!app_dash_profile_get(i, &p)) continue;

        lv_obj_t *row = lv_obj_create(s_modal_list);
        lv_obj_set_size(row, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(row, (i == (uint32_t)s_active_index) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_HEADER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s  (%s, %u rpm)", p.name,
                              p.gauge_style == APP_DASH_GAUGE_ANALOG ? "analogico" : "digital",
                              (unsigned)p.redline_rpm);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_WHITE, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *btn_use = lv_btn_create(row);
        lv_obj_set_size(btn_use, 70, 28);
        lv_obj_align(btn_use, LV_ALIGN_RIGHT_MID, -84, 0);
        lv_obj_set_style_bg_color(btn_use, ZOTTI_GREEN, 0);
        lv_obj_set_style_radius(btn_use, 4, 0);
        lv_obj_add_event_cb(btn_use, profile_use_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl_use = lv_label_create(btn_use);
        lv_label_set_text(lbl_use, "Usar");
        lv_obj_set_style_text_font(lbl_use, ZOTTI_FONT_TINY, 0);
        lv_obj_center(lbl_use);

        if (count > 1) {
            lv_obj_t *btn_del = lv_btn_create(row);
            lv_obj_set_size(btn_del, 70, 28);
            lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, -8, 0);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x4A0000), 0);
            lv_obj_set_style_radius(btn_del, 4, 0);
            lv_obj_add_event_cb(btn_del, profile_delete_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *lbl_del = lv_label_create(btn_del);
            lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
            lv_obj_set_style_text_font(lbl_del, ZOTTI_FONT_TINY, 0);
            lv_obj_center(lbl_del);
        }
    }
}

static void profiles_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_scr || s_modal_overlay) return;

    lv_obj_t *overlay = lv_obj_create(s_scr);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    s_modal_overlay = overlay;

    lv_obj_t *dlg = lv_obj_create(overlay);
    lv_obj_set_size(dlg, 520, 380);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0D1F33), 0);
    lv_obj_set_style_border_color(dlg, ZOTTI_ACCENT, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_radius(dlg, 8, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(dlg);
    lv_label_set_text(lbl_title, LV_SYMBOL_LIST "  PERFIS DO DASHBOARD");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

    // Lista de perfis salvos.
    lv_obj_t *list_cont = lv_obj_create(dlg);
    lv_obj_set_size(list_cont, 490, 170);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 4, 0);
    lv_obj_set_style_pad_row(list_cont, 4, 0);
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    s_modal_list = list_cont;
    rebuild_profile_modal_list();

    // Editor do perfil ativo: estilo + corte.
    lv_obj_t *editor = lv_obj_create(dlg);
    lv_obj_set_size(editor, 490, 90);
    lv_obj_align(editor, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_bg_color(editor, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(editor, 0, 0);
    lv_obj_set_style_radius(editor, 6, 0);
    lv_obj_clear_flag(editor, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_style = lv_btn_create(editor);
    lv_obj_set_size(btn_style, 220, 34);
    lv_obj_align(btn_style, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_set_style_bg_color(btn_style, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_style, 4, 0);
    lv_obj_add_event_cb(btn_style, profile_style_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_modal_lbl_style = lv_label_create(btn_style);
    lv_obj_set_style_text_font(s_modal_lbl_style, ZOTTI_FONT_TINY, 0);
    lv_obj_center(s_modal_lbl_style);

    lv_obj_t *btn_dec = lv_btn_create(editor);
    lv_obj_set_size(btn_dec, 40, 34);
    lv_obj_align(btn_dec, LV_ALIGN_TOP_RIGHT, -170, 10);
    lv_obj_set_style_bg_color(btn_dec, ZOTTI_BG_CARD, 0);
    lv_obj_add_event_cb(btn_dec, profile_redline_dec_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_dec = lv_label_create(btn_dec);
    lv_label_set_text(lbl_dec, "-");
    lv_obj_center(lbl_dec);

    s_modal_lbl_redline = lv_label_create(editor);
    lv_obj_set_style_text_font(s_modal_lbl_redline, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_modal_lbl_redline, ZOTTI_WHITE, 0);
    lv_obj_align(s_modal_lbl_redline, LV_ALIGN_TOP_RIGHT, -70, 20);

    lv_obj_t *btn_inc = lv_btn_create(editor);
    lv_obj_set_size(btn_inc, 40, 34);
    lv_obj_align(btn_inc, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_set_style_bg_color(btn_inc, ZOTTI_BG_CARD, 0);
    lv_obj_add_event_cb(btn_inc, profile_redline_inc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_inc = lv_label_create(btn_inc);
    lv_label_set_text(lbl_inc, "+");
    lv_obj_center(lbl_inc);

    lv_obj_t *btn_save_new = lv_btn_create(editor);
    lv_obj_set_size(btn_save_new, 220, 30);
    lv_obj_align(btn_save_new, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_set_style_bg_color(btn_save_new, ZOTTI_GREEN, 0);
    lv_obj_set_style_radius(btn_save_new, 4, 0);
    lv_obj_add_event_cb(btn_save_new, profile_save_new_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save_new = lv_label_create(btn_save_new);
    lv_label_set_text(lbl_save_new, LV_SYMBOL_SAVE "  Salvar como novo");
    lv_obj_set_style_text_font(lbl_save_new, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_save_new);

    refresh_modal_editor_labels();

    lv_obj_t *btn_close = lv_btn_create(dlg);
    lv_obj_set_size(btn_close, 160, 32);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(btn_close, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_radius(btn_close, 4, 0);
    lv_obj_add_event_cb(btn_close, profile_modal_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE "  Fechar");
    lv_obj_set_style_text_font(lbl_close, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_close);
}

static void demo_toggle_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    bool enable = !app_sim_is_enabled();
    app_sim_set_enabled(enable);
    app_sim_set_redline(s_active_profile.redline_rpm);
    lv_obj_set_style_bg_color(btn, enable ? ZOTTI_YELLOW : ZOTTI_BG_CARD, 0);
}

// Callback voltar ao menu.
static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_arc_rpm) lv_anim_delete(s_arc_rpm, NULL);   // mata anim de arco + anim de corte
    s_redline_flash = false;
    s_scr = s_arc_rpm = s_lbl_rpm = s_lbl_speed = NULL;
    s_lbl_status = NULL;
    s_modal_overlay = s_modal_list = s_modal_lbl_style = s_modal_lbl_redline = NULL;
    for (int i = 0; i < RPM_TICK_COUNT; i++) s_ticks[i] = NULL;
}

// Dashboard.
void ui_screen_dashboard_show(void)
{
    app_dash_profile_init();
    int32_t active = app_dash_profile_get_active_index();
    s_active_index = (active < 0) ? 0 : active;
    if (!app_dash_profile_get((uint32_t)s_active_index, &s_active_profile)) {
        memset(&s_active_profile, 0, sizeof(s_active_profile));
        snprintf(s_active_profile.name, sizeof(s_active_profile.name), "Padrao");
        s_active_profile.redline_rpm = 7000;
    }
    app_sim_set_redline(s_active_profile.redline_rpm);

    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
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
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
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
    lv_label_set_text(lbl_title, "DASHBOARD");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_profiles = lv_btn_create(header);
    lv_obj_set_size(btn_profiles, 90, 28);
    lv_obj_align(btn_profiles, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(btn_profiles, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_radius(btn_profiles, 4, 0);
    lv_obj_add_event_cb(btn_profiles, profiles_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_profiles = lv_label_create(btn_profiles);
    lv_label_set_text(lbl_profiles, LV_SYMBOL_LIST " Perfis");
    lv_obj_set_style_text_font(lbl_profiles, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_profiles);

    lv_obj_t *btn_demo = lv_btn_create(header);
    lv_obj_set_size(btn_demo, 90, 28);
    lv_obj_align(btn_demo, LV_ALIGN_RIGHT_MID, -100, 0);
    lv_obj_set_style_bg_color(btn_demo, app_sim_is_enabled() ? ZOTTI_YELLOW : ZOTTI_BG_CARD, 0);
    lv_obj_set_style_radius(btn_demo, 4, 0);
    lv_obj_add_event_cb(btn_demo, demo_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_demo_btn = lv_label_create(btn_demo);
    lv_label_set_text(lbl_demo_btn, LV_SYMBOL_PLAY " Demo");
    lv_obj_set_style_text_font(lbl_demo_btn, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_demo_btn);

    // Coluna esquerda: RPM.
    lv_obj_t *col_left = lv_obj_create(scr);
    lv_obj_set_size(col_left, 260, 440);
    lv_obj_set_pos(col_left, 0, 40);
    lv_obj_set_style_bg_color(col_left, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(col_left, 0, 0);
    lv_obj_set_style_border_side(col_left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(col_left, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(col_left, 1, 0);
    lv_obj_clear_flag(col_left, LV_OBJ_FLAG_SCROLLABLE);

    // Label "RPM"
    lv_obj_t *lbl_rpm_name = lv_label_create(col_left);
    lv_label_set_text(lbl_rpm_name, "RPM");
    lv_obj_set_style_text_font(lbl_rpm_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_rpm_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_rpm_name, LV_ALIGN_TOP_MID, 0, 12);

    // Arc RPM
    s_arc_rpm = lv_arc_create(col_left);
    lv_obj_set_size(s_arc_rpm, 200, 200);
    lv_obj_align(s_arc_rpm, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_bg_angles(s_arc_rpm, 135, 45);
    lv_arc_set_range(s_arc_rpm, 0, 8000);
    lv_arc_set_value(s_arc_rpm, 0);
    lv_obj_set_style_arc_color(s_arc_rpm, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_rpm, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_rpm, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_rpm, 14, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc_rpm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_arc_rpm, 0, 0);
    lv_obj_set_style_opa(s_arc_rpm, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_rpm, LV_OBJ_FLAG_CLICKABLE);

    // Tiquinhos de graduacao do modo analogico — 6 marcadores ao redor do
    // arco (0/20/40/60/80/100% do range), escondidos por padrao (modo
    // digital). Posicionados uma vez so, na criacao da tela.
    for (int i = 0; i < RPM_TICK_COUNT; i++) {
        float frac  = (float)i / (float)(RPM_TICK_COUNT - 1);
        float deg   = RPM_ARC_START_DEG + frac * RPM_ARC_SWEEP_DEG;
        if (deg >= 360.0f) deg -= 360.0f;
        float rad   = deg * (float)M_PI / 180.0f;
        float r     = 112.0f;
        int32_t dx  = (int32_t)(r * cosf(rad));
        int32_t dy  = (int32_t)(r * sinf(rad));

        lv_obj_t *tick = lv_obj_create(col_left);
        lv_obj_set_size(tick, 7, 7);
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(tick, ZOTTI_ACCENT, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(tick, LV_ALIGN_CENTER, dx, dy - 30);
        lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);   // padrao: modo digital
        s_ticks[i] = tick;
    }

    // Valor RPM dentro do arc
    s_lbl_rpm = lv_label_create(col_left);
    lv_label_set_text(s_lbl_rpm, "0");
    lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_HUGE, 0);
    lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_rpm, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl_rpm_unit = lv_label_create(col_left);
    lv_label_set_text(lbl_rpm_unit, "rpm");
    lv_obj_set_style_text_font(lbl_rpm_unit, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_rpm_unit, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_rpm_unit, LV_ALIGN_CENTER, 0, 5);

    // Labels 0 / 8000
    lv_obj_t *lbl_0 = lv_label_create(col_left);
    lv_label_set_text(lbl_0, "0");
    lv_obj_set_style_text_font(lbl_0, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_0, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_0, LV_ALIGN_CENTER, -88, 55);

    lv_obj_t *lbl_max = lv_label_create(col_left);
    lv_label_set_text(lbl_max, "8k");
    lv_obj_set_style_text_font(lbl_max, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_max, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_max, LV_ALIGN_CENTER, 82, 55);

    // Barra TPS na parte inferior da coluna
    lv_obj_t *lbl_tps_name = lv_label_create(col_left);
    lv_label_set_text(lbl_tps_name, "TPS");
    lv_obj_set_style_text_font(lbl_tps_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_tps_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_tps_name, LV_ALIGN_BOTTOM_LEFT, 10, -55);

    s_bar_tps = lv_bar_create(col_left);
    lv_obj_set_size(s_bar_tps, 220, 12);
    lv_obj_align(s_bar_tps, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(s_bar_tps, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_tps, ZOTTI_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_tps, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_tps, 4, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_tps, 0, 100);
    lv_bar_set_value(s_bar_tps, 0, LV_ANIM_OFF);

    // Coluna central: Velocidade.
    lv_obj_t *col_mid = lv_obj_create(scr);
    lv_obj_set_size(col_mid, 280, 440);
    lv_obj_set_pos(col_mid, 260, 40);
    lv_obj_set_style_bg_opa(col_mid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_mid, 0, 0);
    lv_obj_clear_flag(col_mid, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_speed_name = lv_label_create(col_mid);
    lv_label_set_text(lbl_speed_name, "km/h");
    lv_obj_set_style_text_font(lbl_speed_name, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_speed_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_speed_name, LV_ALIGN_CENTER, 0, 60);

    s_lbl_speed = lv_label_create(col_mid);
    lv_label_set_text(s_lbl_speed, "0");
    lv_obj_set_style_text_font(s_lbl_speed, ZOTTI_FONT_LOGO, 0);
    lv_obj_set_style_text_color(s_lbl_speed, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, 0);

    // MAP no centro-inferior
    lv_obj_t *lbl_map_name = lv_label_create(col_mid);
    lv_label_set_text(lbl_map_name, "MAP");
    lv_obj_set_style_text_font(lbl_map_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_map_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_map_name, LV_ALIGN_BOTTOM_MID, 0, -70);

    s_lbl_map = lv_label_create(col_mid);
    lv_label_set_text(s_lbl_map, "--- kPa");
    lv_obj_set_style_text_font(s_lbl_map, ZOTTI_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(s_lbl_map, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_map, LV_ALIGN_BOTTOM_MID, 0, -42);

    lv_obj_t *sep_mid = lv_obj_create(col_mid);
    lv_obj_set_size(sep_mid, 240, 1);
    lv_obj_align(sep_mid, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(sep_mid, ZOTTI_BORDER, 0);
    lv_obj_set_style_bg_opa(sep_mid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_mid, 0, 0);
    lv_obj_clear_flag(sep_mid, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(col_mid);
    lv_label_set_text(s_lbl_status, LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Coluna direita: Sensores.
    lv_obj_t *col_right = lv_obj_create(scr);
    lv_obj_set_size(col_right, 260, 440);
    lv_obj_set_pos(col_right, 540, 40);
    lv_obj_set_style_bg_color(col_right, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(col_right, 0, 0);
    lv_obj_set_style_border_side(col_right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(col_right, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(col_right, 1, 0);
    lv_obj_clear_flag(col_right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_sensors = lv_label_create(col_right);
    lv_label_set_text(lbl_sensors, "SENSORES");
    lv_obj_set_style_text_font(lbl_sensors, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_sensors, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_sensors, LV_ALIGN_TOP_MID, 0, 10);

    s_lbl_tps  = create_sensor_card(col_right, "TPS",       70);
    s_lbl_afr  = create_sensor_card(col_right, "AFR",      135);
    s_lbl_ect  = create_sensor_card(col_right, "ECT",      200);
    s_lbl_iat  = create_sensor_card(col_right, "IAT",      265);
    s_lbl_batt = create_sensor_card(col_right, "BATERIA",  330);

    apply_gauge_style(s_active_profile.gauge_style);

    // Timer de atualizacao — 200ms, mesma cadencia das outras telas com dado ao vivo.
    s_timer = lv_timer_create(update_timer_cb, 200, NULL);
    update_timer_cb(NULL);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
    ESP_LOGI(TAG, "Dashboard criado (perfil '%s', estilo=%d, corte=%u)",
             s_active_profile.name, (int)s_active_profile.gauge_style,
             (unsigned)s_active_profile.redline_rpm);
}
