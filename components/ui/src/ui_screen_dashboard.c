#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_ecu.h"
#include "app_sim.h"
#include "app_dash_profile.h"
#include "app_dash_minmax.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "UI_DASHBOARD";

// Arco/mostrador do RPM: mesmo range angular usado antes com lv_arc_set_bg_angles
// (135, 45) — 270 graus de varredura, abertura de 90 graus embaixo (visual de
// mostrador automotivo clássico). Usado pra configurar o lv_scale em build_dial().
#define RPM_ARC_START_DEG   135.0f
#define RPM_ARC_SWEEP_DEG   270.0f

// lv_scale_set_line_needle_value: o parametro "needle_length" NAO e
// "0 = raio cheio" (achismo errado que usamos antes) — 0 e literalmente
// comprimento ZERO (ver managed_components/lvgl__lvgl/src/widgets/scale/
// lv_scale.c:245-256: so vira raio cheio quando needle_length >= metade do
// tamanho do mostrador). 1000 e maior que qualquer mostrador usado aqui (o
// maior tem 260px, raio 130px), entao sempre cai no clamp de raio cheio,
// em qualquer chamada, sem precisar saber o tamanho exato do mostrador.
#define DIAL_NEEDLE_LEN     1000

// Referencias de widgets para atualizacao futura.
static lv_obj_t *s_arc_rpm     = NULL;   // so existe no layout Grid (lv_arc)
static lv_obj_t *s_dial_rpm    = NULL;   // Classico/Duplo (lv_scale) — ver build_dial()
static lv_obj_t *s_dial_speed  = NULL;   // Duplo (lv_scale)
static lv_obj_t *s_needle_rpm  = NULL;   // so existe no Estilo Classico (ponteiro)
static lv_obj_t *s_needle_speed = NULL;
static lv_obj_t *s_lbl_rpm     = NULL;
static lv_obj_t *s_lbl_speed   = NULL;
static lv_obj_t *s_bar_tps     = NULL;
static lv_obj_t *s_lbl_map     = NULL;
static lv_obj_t *s_lbl_tps     = NULL;
static lv_obj_t *s_lbl_ect     = NULL;
static lv_obj_t *s_lbl_batt    = NULL;
static lv_obj_t *s_lbl_status  = NULL;

// Widgets exclusivos do layout Race (estilo FuelTech) — só existem quando
// esse layout está ativo.
#define SHIFT_SEGMENTS 10
static lv_obj_t *s_shift_seg[SHIFT_SEGMENTS];
static lv_obj_t *s_lbl_gmeter = NULL;
static lv_obj_t *s_bar_gmeter = NULL;

// Widgets exclusivos do layout Grid (estilo Injepro) — grade de mostradores
// menores, cada um com mínimo/máximo salvo.
typedef enum { GRID_SPEED, GRID_MAP, GRID_TPS, GRID_ECT, GRID_IAT, GRID_BATT, GRID_AFR, GRID_CH_COUNT } grid_ch_t;
static const char *k_grid_names[GRID_CH_COUNT] = { "VEL", "MAP", "TPS", "ECT", "IAT", "BAT", "AFR" };
static lv_obj_t *s_grid_val[GRID_CH_COUNT];
static lv_obj_t *s_grid_mm[GRID_CH_COUNT];

static lv_obj_t   *s_scr           = NULL;
static lv_timer_t *s_timer         = NULL;
static bool        s_redline_flash = false;

static app_dash_profile_t s_active_profile;
static int32_t             s_active_index = 0;

// ─────────────────────────────────────────────────────
// Paleta de cor do acento do RPM por perfil (ROADMAP.md §12) — o campo
// color_theme já existia em app_dash_profile_t, mas nada lia ele pra
// aplicar cor ainda. Afeta só o arco do RPM (existe apenas no layout
// Grid — ver build_grid_layout); os outros layouts usam o mostrador
// lv_scale (s_dial_rpm), que não tem indicador colorível separado.
// Funções expostas (ver ui.h) porque a tela de Config também precisa
// delas pra montar o dropdown "Cor".
// ─────────────────────────────────────────────────────

static const char *k_accent_names[UI_DASH_ACCENT_COUNT] = {
    "Azul", "Verde", "Amarelo", "Vermelho", "Roxo", "Branco",
};

lv_color_t ui_dash_accent_color(uint8_t idx)
{
    switch (idx % UI_DASH_ACCENT_COUNT) {
    case 0:  return ZOTTI_ACCENT;
    case 1:  return ZOTTI_GREEN;
    case 2:  return ZOTTI_YELLOW;
    case 3:  return ZOTTI_RED;
    case 4:  return lv_color_hex(0xB266FF);
    default: return ZOTTI_WHITE;
    }
}

const char *ui_dash_accent_name(uint8_t idx)
{
    return k_accent_names[idx % UI_DASH_ACCENT_COUNT];
}

static void apply_theme_color(void)
{
    if (s_arc_rpm) {
        lv_obj_set_style_arc_color(s_arc_rpm, ui_dash_accent_color(s_active_profile.color_theme), LV_PART_INDICATOR);
    }
}

// ─────────────────────────────────────────────────────
// Efeito de "perto do corte" — pisca o numero (e, no layout Grid, o arco)
// entre a cor normal e vermelho enquanto o RPM estiver acima de 90% do
// redline do perfil ativo. A animacao usa s_lbl_rpm como var porque esse
// label existe em TODOS os layouts (Classico/Race/Grid/Duplo) — s_arc_rpm
// so existe no Grid (os outros usam s_dial_rpm, um lv_scale sem indicador
// de arco pra colorir).
// ─────────────────────────────────────────────────────

static void redline_anim_exec_cb(void *var, int32_t v)
{
    LV_UNUSED(var);
    lv_color_t c = lv_color_mix(ZOTTI_RED, ZOTTI_WHITE, (uint8_t)v);
    if (s_arc_rpm) lv_obj_set_style_arc_color(s_arc_rpm, c, LV_PART_INDICATOR);
    if (s_lbl_rpm) lv_obj_set_style_text_color(s_lbl_rpm, c, 0);
}

static void redline_start(void)
{
    if (s_redline_flash || !s_lbl_rpm) return;
    s_redline_flash = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lbl_rpm);
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
    if (s_arc_rpm)  lv_obj_set_style_arc_color(s_arc_rpm, ui_dash_accent_color(s_active_profile.color_theme), LV_PART_INDICATOR);
    if (s_lbl_rpm) {
        lv_anim_delete(s_lbl_rpm, redline_anim_exec_cb);
        lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
    }
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
    // Essencial: lv_anim_start() NÃO substitui uma animação existente do
    // mesmo (var, exec_cb) sozinho — cada chamada cria uma entrada nova na
    // lista interna do LVGL. Chamando isso a cada 200ms sem apagar a
    // anterior primeiro, vaza memória (cada anim é uma alocação) até
    // esgotar a memória interna (bem menor que a PSRAM nesta placa).
    lv_anim_delete(arc, arc_anim_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec_cb);
    lv_anim_set_values(&a, lv_arc_get_value(arc), value);
    lv_anim_set_time(&a, 180);
    lv_anim_start(&a);
}

// Move o ponteiro de um mostrador lv_scale (Estilo Classico) pro valor
// atual. Sem animacao propria (diferente do arco): lv_scale_set_line_needle_value
// so invalida a bounding box da linha do ponteiro, muito mais barato que a
// cunha anti-aliased do arco — nao precisa do mesmo cuidado de dedup que
// animate_arc_to() tem acima.
static void update_dial_needle(lv_obj_t *scale, lv_obj_t *needle, int32_t value)
{
    if (!scale || !needle) return;
    lv_scale_set_line_needle_value(scale, needle, DIAL_NEEDLE_LEN, value);
}

// ─────────────────────────────────────────────────────
// Barra de shift light (layout Race, estilo FuelTech) — segmentos acendem
// progressivamente conforme o RPM sobe; verde/amarelo/vermelho por faixa.
// ─────────────────────────────────────────────────────

static void update_shift_bar(int32_t rpm, uint16_t redline)
{
    if (!s_shift_seg[0] || redline == 0) return;

    for (int i = 0; i < SHIFT_SEGMENTS; i++) {
        // Deixa 2 "segmentos" de folga acima do redline — os 10 acendem
        // um pouco antes do corte, não só exatamente nele.
        float threshold = (float)redline * (float)(i + 1) / (float)(SHIFT_SEGMENTS + 2);
        bool  lit = (float)rpm >= threshold;
        lv_color_t base = (i < 5) ? ZOTTI_GREEN : (i < 8) ? ZOTTI_YELLOW : ZOTTI_RED;
        lv_obj_set_style_bg_color(s_shift_seg[i], base, 0);
        lv_obj_set_style_bg_opa(s_shift_seg[i], lit ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

// ─────────────────────────────────────────────────────
// G-meter (layout Race) — aceleração longitudinal (accel_g do app_ecu/app_sim).
// Barra simétrica: enche a partir do centro pro lado positivo ou negativo.
// ─────────────────────────────────────────────────────

static void update_gmeter(float accel_g)
{
    if (!s_lbl_gmeter) return;
    lv_label_set_text_fmt(s_lbl_gmeter, "%+.2f G", (double)accel_g);

    if (s_bar_gmeter) {
        int32_t bar_val = (int32_t)(accel_g * 100.0f);
        if (bar_val > 100) bar_val = 100;
        if (bar_val < -100) bar_val = -100;
        lv_bar_set_value(s_bar_gmeter, bar_val, LV_ANIM_ON);
    }
}

// ─────────────────────────────────────────────────────
// Grade de mostradores com mínimo/máximo (layout Grid, estilo Injepro).
// ─────────────────────────────────────────────────────

static void update_grid_tiles(float speed, float map_kpa, float tps, float ect,
                               float iat, float batt, float afr)
{
    if (!s_grid_val[0]) return;

    float vals[GRID_CH_COUNT] = { speed, map_kpa, tps, ect, iat, batt, afr };

    app_dash_minmax_t mm;
    app_dash_minmax_get(&mm);
    float mins[GRID_CH_COUNT] = { mm.speed_min, mm.map_min, mm.tps_min, mm.ect_min, mm.iat_min, mm.batt_min, mm.afr_min };
    float maxs[GRID_CH_COUNT] = { mm.speed_max, mm.map_max, mm.tps_max, mm.ect_max, mm.iat_max, mm.batt_max, mm.afr_max };

    for (int i = 0; i < GRID_CH_COUNT; i++) {
        if (!s_grid_val[i]) continue;
        lv_label_set_text_fmt(s_grid_val[i], "%.1f", (double)vals[i]);
        if (s_grid_mm[i]) {
            lv_label_set_text_fmt(s_grid_mm[i], "%.0f / %.0f", (double)mins[i], (double)maxs[i]);
        }
    }
}

// Atualiza os dados da dashboard. accel_g vem do app_sim (demo) ou é 0 pra
// dados reais do app_ecu ainda (o protocolo v1 do app_ecu não manda
// aceleração — ver ROADMAP.md se isso precisar entrar no futuro).
void ui_screen_dashboard_update(int32_t rpm, int32_t speed_kph,
                                 int32_t map_kpa, int32_t tps_pct,
                                 float afr, int32_t ect_c,
                                 int32_t iat_c, float batt_v, float accel_g)
{
    // Mín/máx acompanha independente de qual layout está na tela (assim
    // como no Injepro, os recordes ficam guardados mesmo trocando de tela).
    app_dash_minmax_update((float)rpm, (float)speed_kph, (float)map_kpa, (float)tps_pct,
                          (float)ect_c, (float)iat_c, batt_v, afr);

    if (s_arc_rpm) animate_arc_to(s_arc_rpm, rpm);   // layout Grid (lv_arc)
    update_dial_needle(s_dial_rpm, s_needle_rpm, rpm);            // Classico/Duplo, Estilo Classico
    update_dial_needle(s_dial_speed, s_needle_speed, speed_kph);  // Duplo, Estilo Classico
    if (s_lbl_rpm)   lv_label_set_text_fmt(s_lbl_rpm,   "%ld", (long)rpm);
    if (s_lbl_speed) lv_label_set_text_fmt(s_lbl_speed, "%ld", (long)speed_kph);
    if (s_bar_tps)   lv_bar_set_value(s_bar_tps, tps_pct, LV_ANIM_ON);
    if (s_lbl_map)   lv_label_set_text_fmt(s_lbl_map,  "%ld kPa", (long)map_kpa);
    if (s_lbl_tps)   lv_label_set_text_fmt(s_lbl_tps,  "%ld%%", (long)tps_pct);
    if (s_lbl_ect)  lv_label_set_text_fmt(s_lbl_ect,  "%ld C", (long)ect_c);
    if (s_lbl_batt)  lv_label_set_text_fmt(s_lbl_batt, "%.1fV", batt_v);

    // Cor do ECT: verde < 90 C, amarelo 90-105 C, vermelho > 105 C.
    if (s_lbl_ect) {
        lv_color_t ect_color = (ect_c < 90)  ? ZOTTI_GREEN  :
                               (ect_c < 105) ? ZOTTI_YELLOW : ZOTTI_RED;
        lv_obj_set_style_text_color(s_lbl_ect, ect_color, 0);
    }

    update_shift_bar(rpm, s_active_profile.redline_rpm);
    update_gmeter(accel_g);
    update_grid_tiles((float)speed_kph, (float)map_kpa, (float)tps_pct,
                       (float)ect_c, (float)iat_c, batt_v, afr);

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
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, LV_SYMBOL_PLAY " DEMO ATIVO");
            lv_obj_set_style_text_color(s_lbl_status, ZOTTI_YELLOW, 0);
        }
        ui_screen_dashboard_update(d.rpm, d.speed_kph, d.map_kpa, d.tps_pct,
                                   d.lambda * 14.7f, d.ect_c, d.iat_c, d.batt_v, d.accel_g);
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
                               d.lambda * 14.7f, d.ect_c, d.iat_c, d.batt_v, 0.0f);
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

// Zera as referencias de widgets que so existem em ALGUNS layouts/Estilos
// (mostrador redondo, ponteiro, shift-light, grade do layout Grid, etc).
// Chamada em dois momentos:
//   1) no INICIO de ui_screen_dashboard_show(), antes de construir a tela
//      nova — essencial, porque depender so do DELETE da tela antiga e
//      tarde demais: o fade de troca de tela leva ~200ms, e nesse meio
//      tempo o timer de 200ms pode rodar e usar um ponteiro que aponta pra
//      um widget da tela ANTIGA que o layout/Estilo NOVO nao recriou (ex:
//      trocar pro Estilo Classico no layout Classico nao usa s_shift_seg;
//      sem isso aqui o array continuaria apontando pros segmentos da tela
//      antiga ate ela ser apagada, e dai vira ponteiro solto = crash).
//   2) no screen_delete_cb de verdade (so quando a tela apagada e a atual),
//      pra nao deixar lixo quando o usuario sai do dashboard de vez.
static void reset_optional_widget_refs(void)
{
    s_arc_rpm = s_lbl_rpm = s_lbl_speed = NULL;
    s_dial_rpm = s_dial_speed = s_needle_rpm = s_needle_speed = NULL;
    s_lbl_map = s_lbl_tps = s_lbl_ect = s_lbl_batt = NULL;
    s_bar_tps = NULL;
    s_lbl_status = NULL;
    s_lbl_gmeter = s_bar_gmeter = NULL;
    for (int i = 0; i < SHIFT_SEGMENTS; i++) s_shift_seg[i] = NULL;
    for (int i = 0; i < GRID_CH_COUNT; i++) { s_grid_val[i] = NULL; s_grid_mm[i] = NULL; }
}

static void screen_delete_cb(lv_event_t *e)
{
    // ui_screen_dashboard_show() troca de tela com lv_scr_load_anim(...,
    // auto_del=true) — a tela ANTIGA so e apagada de verdade ~200ms depois
    // (duracao do fade), nao na hora. Quando o proprio dashboard chama
    // ui_screen_dashboard_show() de novo (troca de Estilo/Layout), esse
    // DELETE atrasado da tela antiga dispara DEPOIS que s_scr/s_timer/etc
    // ja apontam pra tela NOVA — sem esse guard, ele zerava por baixo o
    // timer e os widgets da tela que acabou de ser criada, e o Demo (e todo
    // o resto) parava de atualizar ate voltar pro menu e reabrir o
    // dashboard do zero.
    if (lv_event_get_target(e) != s_scr) return;

    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_arc_rpm) lv_anim_delete(s_arc_rpm, NULL);   // mata anim do arco (layout Grid)
    if (s_lbl_rpm) lv_anim_delete(s_lbl_rpm, NULL);   // mata anim de corte (redline_start)
    s_redline_flash = false;
    s_scr = NULL;
    reset_optional_widget_refs();
}

// ─────────────────────────────────────────────────────
// Mostrador redondo com ponteiro de verdade — usado SO pelo Estilo
// Classico (analogico): tiquinhos numerados ao redor do anel + ponteiro de
// linha real (lv_scale_set_line_needle_value), igual um velocimetro/
// tacometro antigo. O Estilo Sport (digital) NAO usa mostrador redondo —
// ver build_shift_bar() e os blocos digitais em build_classic_layout()/
// build_twin_layout(). Mesmo angulo de varredura do arco antigo (135 graus
// de inicio, 270 graus de giro) pra nao mudar o posicionamento do resto de
// cada tela.
// ─────────────────────────────────────────────────────

static void build_dial(lv_obj_t *parent, int32_t size, int32_t min, int32_t max,
                        lv_obj_t **out_scale, lv_obj_t **out_needle)
{
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_set_size(scale, size, size);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(scale, min, max);
    lv_scale_set_angle_range(scale, (uint32_t)RPM_ARC_SWEEP_DEG);
    lv_scale_set_rotation(scale, (int32_t)RPM_ARC_START_DEG);
    lv_scale_set_total_tick_count(scale, 21);
    lv_scale_set_major_tick_every(scale, 5);
    lv_scale_set_label_show(scale, true);
    lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scale, 0, 0);
    lv_obj_set_style_arc_color(scale, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(scale, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(scale, ZOTTI_GRAY, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, ZOTTI_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 14, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, ZOTTI_GRAY, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(scale, ZOTTI_FONT_TINY, LV_PART_INDICATOR);
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_CLICKABLE);
    // Essencial: lv_scale, diferente do lv_arc que ele substituiu, vem com
    // LV_OBJ_FLAG_SCROLLABLE ligado por padrao. Sem apagar essa flag, o
    // toque no mostrador (ou so o layout mudando) deixa a TELA INTEIRA
    // "arrastavel" pro lado — o scroll da tela root inteiro por causa da
    // flag LV_OBJ_FLAG_SCROLL_CHAIN, tambem ligada por padrao, que propaga
    // o gesto de um filho pro pai. Mesma classe de "flag padrao esquecida"
    // de outros bugs ja corrigidos nesta tela (todo lv_obj_create() daqui
    // do arquivo limpa essa flag, exceto este que foi adicionado sem).
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *needle = lv_line_create(scale);
    lv_obj_set_style_line_color(needle, ZOTTI_ACCENT, 0);
    lv_obj_set_style_line_width(needle, 4, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    lv_scale_set_line_needle_value(scale, needle, DIAL_NEEDLE_LEN, min);
    if (out_needle) *out_needle = needle;

    if (out_scale) *out_scale = scale;
}

// Barra continua de shift-light (Estilo Sport) — reaproveita
// s_shift_seg[]/update_shift_bar(), o mesmo mecanismo ja usado no layout
// Race (verde/amarelo/vermelho, acende progressivamente conforme o RPM
// sobe em direcao ao corte), so que os segmentos ficam colados formando
// uma barra continua em vez de barras espacadas, pra bater com o visual de
// referencia (RPM digital + barra colorida no topo, sem mostrador redondo).
static void build_shift_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t seg_w = width / SHIFT_SEGMENTS;
    for (int i = 0; i < SHIFT_SEGMENTS; i++) {
        lv_obj_t *seg = lv_obj_create(parent);
        lv_obj_set_size(seg, seg_w - 2, height);
        lv_obj_set_pos(seg, x + i * seg_w, y);
        lv_obj_set_style_radius(seg, 2, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_bg_color(seg, ZOTTI_GRAY_DARK, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_20, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        s_shift_seg[i] = seg;
    }
}

// Anel de pontinhos de shift-light em volta de um mostrador redondo (Estilo
// Classico no layout Duplo) — reaproveita s_shift_seg[]/update_shift_bar(),
// so que os pontos ficam distribuidos ao longo do MESMO arco de 270 graus
// do mostrador (RPM_ARC_START_DEG/SWEEP_DEG), um pouco pra fora do anel,
// em vez de enfileirados. "dial" e "parent" devem ter o mesmo pai (mesma
// logica do lv_obj_align_to usado em build_shift_bar).
static void build_shift_ring(lv_obj_t *parent, lv_obj_t *dial, int32_t dial_size)
{
    float radius = (float)dial_size / 2.0f + 16.0f;
    for (int i = 0; i < SHIFT_SEGMENTS; i++) {
        float frac = (float)i / (float)(SHIFT_SEGMENTS - 1);
        float deg  = RPM_ARC_START_DEG + frac * RPM_ARC_SWEEP_DEG;
        if (deg >= 360.0f) deg -= 360.0f;
        float rad  = deg * (float)M_PI / 180.0f;
        int32_t dx = (int32_t)(radius * cosf(rad));
        int32_t dy = (int32_t)(radius * sinf(rad));

        lv_obj_t *seg = lv_obj_create(parent);
        lv_obj_set_size(seg, 14, 14);
        lv_obj_set_style_radius(seg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_bg_color(seg, ZOTTI_GRAY_DARK, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_20, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align_to(seg, dial, LV_ALIGN_CENTER, dx, dy);
        s_shift_seg[i] = seg;
    }
}

// ─────────────────────────────────────────────────────
// Layout Classico — arco/digital de RPM + 3 colunas (o layout original).
// ─────────────────────────────────────────────────────

static void build_classic_layout(lv_obj_t *scr)
{
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

    // Mostrador de RPM. "sport" reflete o Estilo salvo no perfil ativo:
    //   Sport (digital):   SEM mostrador redondo nenhum — so numero grande +
    //                       barra continua de shift-light (build_shift_bar).
    //   Classico (analog): mostrador redondo com ticks + ponteiro de verdade
    //                       (build_dial).
    bool sport = (s_active_profile.gauge_style == APP_DASH_GAUGE_DIGITAL);

    if (sport) {
        s_lbl_rpm = lv_label_create(col_left);
        lv_label_set_text(s_lbl_rpm, "0");
        lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_HUGE, 0);
        lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_rpm, LV_ALIGN_TOP_MID, 0, 55);

        lv_obj_t *lbl_rpm_unit = lv_label_create(col_left);
        lv_label_set_text(lbl_rpm_unit, "rpm");
        lv_obj_set_style_text_font(lbl_rpm_unit, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_rpm_unit, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_rpm_unit, LV_ALIGN_TOP_MID, 0, 110);

        build_shift_bar(col_left, 20, 145, 220, 16);
    } else {
        build_dial(col_left, 200, 0, 8000, &s_dial_rpm, &s_needle_rpm);
        lv_obj_align(s_dial_rpm, LV_ALIGN_CENTER, 0, -30);

        s_lbl_rpm = lv_label_create(col_left);
        lv_label_set_text(s_lbl_rpm, "0");
        lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_LARGE, 0);
        lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_rpm, LV_ALIGN_CENTER, 0, 15);

        lv_obj_t *lbl_rpm_unit = lv_label_create(col_left);
        lv_label_set_text(lbl_rpm_unit, "rpm");
        lv_obj_set_style_text_font(lbl_rpm_unit, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_rpm_unit, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_rpm_unit, LV_ALIGN_CENTER, 0, 40);
        // Os numeros dos ticks (0, 2k, 4k...) ja vem do proprio lv_scale
        // (lv_scale_set_label_show em build_dial) — sem labels manuais aqui.
    }

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

    // Velocidade: numero grande (Sport) ou mostrador redondo com ponteiro
    // (Classico) — mesmo criterio da coluna de RPM ao lado.
    if (sport) {
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
    } else {
        build_dial(col_mid, 200, 0, 220, &s_dial_speed, &s_needle_speed);
        lv_obj_align(s_dial_speed, LV_ALIGN_CENTER, 0, -30);

        s_lbl_speed = lv_label_create(s_dial_speed);
        lv_label_set_text(s_lbl_speed, "0");
        lv_obj_set_style_text_font(s_lbl_speed, ZOTTI_FONT_LARGE, 0);
        lv_obj_set_style_text_color(s_lbl_speed, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, 15);

        lv_obj_t *lbl_speed_name = lv_label_create(s_dial_speed);
        lv_label_set_text(lbl_speed_name, "km/h");
        lv_obj_set_style_text_font(lbl_speed_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_speed_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_speed_name, LV_ALIGN_CENTER, 0, 40);
    }

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

    // So o necessario: TPS, ECT e BATERIA (MAP ja fica na coluna do meio,
    // junto com a velocidade — AFR e IAT saem daqui pra nao poluir a tela).
    s_lbl_tps  = create_sensor_card(col_right, "TPS",      120);
    s_lbl_ect  = create_sensor_card(col_right, "ECT",      230);
    s_lbl_batt = create_sensor_card(col_right, "BATERIA",  340);
}

// ─────────────────────────────────────────────────────
// Layout Race (estilo FuelTech) — RPM digital grande, barra de shift light
// progressiva, G-meter, sensores essenciais.
// ─────────────────────────────────────────────────────

static void build_race_layout(lv_obj_t *scr)
{
    // RPM digital enorme, centralizado.
    s_lbl_rpm = lv_label_create(scr);
    lv_label_set_text(s_lbl_rpm, "0");
    lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_LOGO, 0);
    lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_rpm, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *lbl_rpm_unit = lv_label_create(scr);
    lv_label_set_text(lbl_rpm_unit, "RPM");
    lv_obj_set_style_text_font(lbl_rpm_unit, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_rpm_unit, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_rpm_unit, LV_ALIGN_CENTER, 0, 30);

    // Barra de shift light — 10 segmentos logo abaixo do cabecalho.
    int32_t seg_w = 70, seg_h = 34, gap = 6;
    int32_t total_w = SHIFT_SEGMENTS * seg_w + (SHIFT_SEGMENTS - 1) * gap;
    int32_t start_x = (800 - total_w) / 2;
    for (int i = 0; i < SHIFT_SEGMENTS; i++) {
        lv_obj_t *seg = lv_obj_create(scr);
        lv_obj_set_size(seg, seg_w, seg_h);
        lv_obj_set_pos(seg, start_x + i * (seg_w + gap), 52);
        lv_obj_set_style_radius(seg, 4, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_bg_color(seg, ZOTTI_GRAY_DARK, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_20, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        s_shift_seg[i] = seg;
    }

    // G-meter: numero + barra simetrica (enche a partir do centro).
    lv_obj_t *lbl_g_name = lv_label_create(scr);
    lv_label_set_text(lbl_g_name, "ACELERACAO (G)");
    lv_obj_set_style_text_font(lbl_g_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_g_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_g_name, LV_ALIGN_BOTTOM_MID, 0, -110);

    s_lbl_gmeter = lv_label_create(scr);
    lv_label_set_text(s_lbl_gmeter, "+0.00 G");
    lv_obj_set_style_text_font(s_lbl_gmeter, ZOTTI_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(s_lbl_gmeter, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_gmeter, LV_ALIGN_BOTTOM_MID, 0, -86);

    s_bar_gmeter = lv_bar_create(scr);
    lv_obj_set_size(s_bar_gmeter, 400, 14);
    lv_obj_align(s_bar_gmeter, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_bar_set_mode(s_bar_gmeter, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(s_bar_gmeter, -100, 100);
    lv_bar_set_value(s_bar_gmeter, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_gmeter, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_gmeter, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_gmeter, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_gmeter, 4, LV_PART_INDICATOR);

    // Faixa de sensores essenciais, parte inferior.
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, 780, 70);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(row, LV_FLEX_FLOW_ROW, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_speed = create_sensor_card(row, "km/h",   0);
    s_lbl_map   = create_sensor_card(row, "MAP",    0);
    s_lbl_tps   = create_sensor_card(row, "TPS",    0);
    s_lbl_ect   = create_sensor_card(row, "ECT",    0);
    s_lbl_batt  = create_sensor_card(row, "BATERIA",0);

    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 10, -84);
}

// ─────────────────────────────────────────────────────
// Layout Grid (estilo Injepro) — numerico central + ponteiro analogico +
// grade de mostradores menores com minimo/maximo salvo.
// ─────────────────────────────────────────────────────

static void build_grid_layout(lv_obj_t *scr)
{
    // Arco + digital central (mesmo arco do classico, so que centralizado
    // sozinho — sem colunas ao lado).
    s_arc_rpm = lv_arc_create(scr);
    lv_obj_set_size(s_arc_rpm, 220, 220);
    lv_obj_align(s_arc_rpm, LV_ALIGN_TOP_MID, 0, 50);
    lv_arc_set_bg_angles(s_arc_rpm, 135, 45);
    lv_arc_set_range(s_arc_rpm, 0, 8000);
    lv_arc_set_value(s_arc_rpm, 0);
    lv_obj_set_style_arc_color(s_arc_rpm, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_rpm, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_rpm, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_rpm, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc_rpm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_arc_rpm, 0, 0);
    lv_obj_set_style_opa(s_arc_rpm, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_arc_rpm, ZOTTI_WHITE, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_rpm, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_rpm = lv_label_create(scr);
    lv_label_set_text(s_lbl_rpm, "0");
    lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_rpm, LV_ALIGN_TOP_MID, 0, 145);

    lv_obj_t *lbl_rpm_unit = lv_label_create(scr);
    lv_label_set_text(lbl_rpm_unit, "rpm");
    lv_obj_set_style_text_font(lbl_rpm_unit, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_rpm_unit, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_rpm_unit, LV_ALIGN_TOP_MID, 0, 185);

    // Grade de mostradores menores, 4 colunas x 2 linhas, cada um com
    // mínimo/máximo salvo (persistido — ver app_dash_minmax).
    int32_t tile_w = 190, tile_h = 90, gx = 5, gy = 5;
    int32_t start_x = (800 - (4 * tile_w + 3 * gx)) / 2;
    int32_t start_y = 260;
    for (int i = 0; i < GRID_CH_COUNT; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t *tile = lv_obj_create(scr);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_pos(tile, start_x + col * (tile_w + gx), start_y + row * (tile_h + gy));
        lv_obj_set_style_bg_color(tile, ZOTTI_BG_CARD, 0);
        lv_obj_set_style_border_color(tile, ZOTTI_BORDER, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_radius(tile, 6, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(tile);
        lv_label_set_text(lbl_name, k_grid_names[i]);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 8, 6);

        s_grid_val[i] = lv_label_create(tile);
        lv_label_set_text(s_grid_val[i], "---");
        lv_obj_set_style_text_font(s_grid_val[i], ZOTTI_FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(s_grid_val[i], ZOTTI_WHITE, 0);
        lv_obj_align(s_grid_val[i], LV_ALIGN_LEFT_MID, 8, 6);

        s_grid_mm[i] = lv_label_create(tile);
        lv_label_set_text(s_grid_mm[i], "--- / ---");
        lv_obj_set_style_text_font(s_grid_mm[i], ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(s_grid_mm[i], ZOTTI_GRAY, 0);
        lv_obj_align(s_grid_mm[i], LV_ALIGN_BOTTOM_LEFT, 8, -6);
    }

    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_RIGHT, -10, 44);
}

// ─────────────────────────────────────────────────────
// Layout Duplo — dois mostradores redondos lado a lado (RPM + velocidade),
// o arranjo clássico de painel automotivo (tacômetro + velocímetro), com a
// paleta âmbar/creme do TEMA3 dando o tom de painel analógico antigo.
// ─────────────────────────────────────────────────────

static void build_twin_layout(lv_obj_t *scr)
{
    const int32_t dial_size = 260;
    const int32_t dial_y    = 70;
    const int32_t left_x    = 60;
    const int32_t right_x   = 800 - 60 - dial_size;
    bool sport = (s_active_profile.gauge_style == APP_DASH_GAUGE_DIGITAL);

    if (sport) {
        // Sem mostrador redondo nenhum — barra continua de shift-light logo
        // abaixo do cabecalho (mesmo visual da imagem de referencia) +
        // numero de RPM a esquerda, numero de velocidade a direita.
        build_shift_bar(scr, 40, 50, 720, 18);

        s_lbl_rpm = lv_label_create(scr);
        lv_label_set_text(s_lbl_rpm, "0");
        lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_LOGO, 0);
        lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
        lv_obj_set_pos(s_lbl_rpm, left_x, 85);

        lv_obj_t *lbl_rpm_name = lv_label_create(scr);
        lv_label_set_text(lbl_rpm_name, "RPM");
        lv_obj_set_style_text_font(lbl_rpm_name, ZOTTI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_rpm_name, ZOTTI_GRAY, 0);
        lv_obj_set_pos(lbl_rpm_name, left_x, 175);

        s_lbl_speed = lv_label_create(scr);
        lv_label_set_text(s_lbl_speed, "0");
        lv_obj_set_style_text_font(s_lbl_speed, ZOTTI_FONT_LOGO, 0);
        lv_obj_set_style_text_color(s_lbl_speed, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_speed, LV_ALIGN_TOP_RIGHT, -(800 - right_x - dial_size), 85);

        lv_obj_t *lbl_speed_name = lv_label_create(scr);
        lv_label_set_text(lbl_speed_name, "km/h");
        lv_obj_set_style_text_font(lbl_speed_name, ZOTTI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_speed_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_speed_name, LV_ALIGN_TOP_RIGHT, -(800 - right_x - dial_size), 175);

        // Faixa de leituras secundarias — so no Sport. No Classico os
        // sensores ficam de fora de proposito (so os 2 relogios + o anel de
        // shift em volta do RPM, ver mais abaixo).
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_set_size(row, 780, 70);
        lv_obj_set_pos(row, 10, 362);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_style_flex_flow(row, LV_FLEX_FLOW_ROW, 0);
        lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_map  = create_sensor_card(row, "MAP",     0);
        s_lbl_tps  = create_sensor_card(row, "TPS",     0);
        s_lbl_ect  = create_sensor_card(row, "MOTOR",   0);
        s_lbl_batt = create_sensor_card(row, "BATERIA", 0);
    } else {
        // Mostrador RPM (esquerda) — redondo, com ticks + ponteiro.
        build_dial(scr, dial_size, 0, 8000, &s_dial_rpm, &s_needle_rpm);
        lv_obj_set_pos(s_dial_rpm, left_x, dial_y);

        // Anel de pontinhos de shift-light em volta do mostrador de RPM
        // (verde -> laranja -> vermelho, acende conforme o RPM sobe —
        // update_shift_bar ja faz isso, so precisa dos segmentos existirem).
        build_shift_ring(scr, s_dial_rpm, dial_size);

        // Numero e unidade ficam DENTRO do proprio mostrador (filhos dele) —
        // assim centralizam sozinhos, sem precisar calcular posicao manual.
        s_lbl_rpm = lv_label_create(s_dial_rpm);
        lv_label_set_text(s_lbl_rpm, "0");
        lv_obj_set_style_text_font(s_lbl_rpm, ZOTTI_FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(s_lbl_rpm, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_rpm, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *lbl_rpm_name = lv_label_create(s_dial_rpm);
        lv_label_set_text(lbl_rpm_name, "RPM");
        lv_obj_set_style_text_font(lbl_rpm_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_rpm_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_rpm_name, LV_ALIGN_CENTER, 0, 54);

        // Mostrador velocidade (direita) — mesmo tratamento do de RPM.
        build_dial(scr, dial_size, 0, 220, &s_dial_speed, &s_needle_speed);
        lv_obj_set_pos(s_dial_speed, right_x, dial_y);

        s_lbl_speed = lv_label_create(s_dial_speed);
        lv_label_set_text(s_lbl_speed, "0");
        lv_obj_set_style_text_font(s_lbl_speed, ZOTTI_FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(s_lbl_speed, ZOTTI_WHITE, 0);
        lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *lbl_speed_name = lv_label_create(s_dial_speed);
        lv_label_set_text(lbl_speed_name, "km/h");
        lv_obj_set_style_text_font(lbl_speed_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_speed_name, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_speed_name, LV_ALIGN_CENTER, 0, 54);
    }

    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 6);
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

    // Essencial ANTES de criar a tela nova — ver o comentario grande em
    // reset_optional_widget_refs() sobre por que nao da pra confiar so no
    // DELETE (atrasado ~200ms) da tela antiga pra isso.
    reset_optional_widget_refs();

    // Apaga o timer de atualizacao da chamada ANTERIOR desta funcao, se
    // houver (troca de Estilo/Layout chama ui_screen_dashboard_show() de
    // novo com o dashboard ja aberto). lv_timer_t NAO e filho da tela —
    // nao morre sozinho quando a tela e apagada — entao sem isso cada troca
    // deixava um timer de 200ms orfao rodando pra sempre, empilhando um a
    // cada troca.
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }

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

    // Estilo/Modelo/Corte do dashboard agora se ajustam na tela de Config
    // (junto do Tema) — sem botao "Perfis" nem modal aqui.
    lv_obj_t *btn_demo = lv_btn_create(header);
    lv_obj_set_size(btn_demo, 90, 28);
    lv_obj_align(btn_demo, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(btn_demo, app_sim_is_enabled() ? ZOTTI_YELLOW : ZOTTI_BG_CARD, 0);
    lv_obj_set_style_radius(btn_demo, 4, 0);
    lv_obj_add_event_cb(btn_demo, demo_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_demo_btn = lv_label_create(btn_demo);
    lv_label_set_text(lbl_demo_btn, LV_SYMBOL_PLAY " Demo");
    lv_obj_set_style_text_font(lbl_demo_btn, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_demo_btn);

    // Corpo da tela — arranjo inteiro depende do "modelo" (layout) do
    // perfil ativo. Cada builder cria seu próprio conjunto de widgets.
    switch (s_active_profile.layout) {
    case APP_DASH_LAYOUT_RACE:
        build_race_layout(scr);
        break;
    case APP_DASH_LAYOUT_GRID:
        build_grid_layout(scr);
        break;
    case APP_DASH_LAYOUT_DUPLO:
        build_twin_layout(scr);
        break;
    case APP_DASH_LAYOUT_CLASSIC:
    default:
        build_classic_layout(scr);
        break;
    }
    apply_theme_color();

    // Timer de atualizacao — 200ms, mesma cadencia das outras telas com dado ao vivo.
    s_timer = lv_timer_create(update_timer_cb, 33, NULL);
    update_timer_cb(NULL);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
    ESP_LOGI(TAG, "Dashboard criado (perfil '%s', estilo=%d, corte=%u)",
             s_active_profile.name, (int)s_active_profile.gauge_style,
             (unsigned)s_active_profile.redline_rpm);
}
