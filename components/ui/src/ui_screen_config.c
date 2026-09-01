#include "ui.h"
#include "zotti_theme.h"
#include "zotti_brightness.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_ble.h"
#include "app_dash_profile.h"
#include "app_dash_minmax.h"

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
static lv_obj_t *s_lbl_brilho;

static void config_screen_delete_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_scr) return;
    app_ble_scan_stop();
    if (s_ble_timer) {
        lv_timer_delete(s_ble_timer);
        s_ble_timer = NULL;
    }
    s_ble_list_cont = NULL;
    s_ble_status_lbl = NULL;
    s_lbl_brilho = NULL;
    s_scr = NULL;
}

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

static void tema_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    // A troca só aparece em telas abertas depois disso — ver zotti_theme.h.
    zotti_theme_set((zotti_theme_id_t)sel);
}

// Este hardware não tem PWM de backlight (é liga/desliga via CH422G, ver
// zotti_brightness.h) — "Brilho" aqui é esmaecimento por software, efeito
// imediato em qualquer tela já aberta (superposição no layer superior).
static void brilho_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    zotti_brightness_set((uint8_t)val);
    if (s_lbl_brilho) lv_label_set_text_fmt(s_lbl_brilho, "%d%%", (int)val);
}

// ─────────────────────────────────────────────────────
// Estilo / Modelo / Corte do dashboard — configuracao unica e global (nao
// existe mais tela de "Perfis" com lista/editor/Salvar dentro do
// dashboard; aplica na hora e persiste sozinho, igual o Tema acima).
// Sempre le/grava o perfil "ativo" (indice de app_dash_profile_get_active_index(),
// ou 0 se nenhum foi marcado ainda — mesmo fallback que ui_screen_dashboard
// já usava).
// ─────────────────────────────────────────────────────

static lv_obj_t *s_lbl_corte;

static uint32_t active_dash_profile_index(void)
{
    int32_t idx = app_dash_profile_get_active_index();
    return (idx < 0) ? 0 : (uint32_t)idx;
}

static bool get_active_dash_profile(app_dash_profile_t *out)
{
    app_dash_profile_init();
    return app_dash_profile_get(active_dash_profile_index(), out);
}

static void save_active_dash_profile(const app_dash_profile_t *p)
{
    app_dash_profile_save(active_dash_profile_index(), p);
}

static void refresh_corte_label(void)
{
    if (!s_lbl_corte) return;
    app_dash_profile_t p;
    if (get_active_dash_profile(&p)) {
        lv_label_set_text_fmt(s_lbl_corte, "%u rpm", (unsigned)p.redline_rpm);
    }
}

static void estilo_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);   // 0=Sport, 1=Classico

    app_dash_profile_t p;
    if (!get_active_dash_profile(&p)) return;
    p.gauge_style = (sel == 1) ? APP_DASH_GAUGE_ANALOG : APP_DASH_GAUGE_DIGITAL;
    save_active_dash_profile(&p);
}

static void modelo_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);   // mesma ordem de app_dash_layout_t

    app_dash_profile_t p;
    if (!get_active_dash_profile(&p)) return;
    p.layout = (app_dash_layout_t)sel;
    save_active_dash_profile(&p);
}

static void cor_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);   // mesma ordem de ui_dash_accent_*

    app_dash_profile_t p;
    if (!get_active_dash_profile(&p)) return;
    p.color_theme = (uint8_t)sel;
    save_active_dash_profile(&p);
}

static void corte_dec_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_dash_profile_t p;
    if (!get_active_dash_profile(&p)) return;
    if (p.redline_rpm > 3250) p.redline_rpm -= 250;
    save_active_dash_profile(&p);
    refresh_corte_label();
}

static void corte_inc_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_dash_profile_t p;
    if (!get_active_dash_profile(&p)) return;
    if (p.redline_rpm < 12000) p.redline_rpm += 250;
    save_active_dash_profile(&p);
    refresh_corte_label();
}

static void reset_minmax_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_dash_minmax_reset();
}

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
    lv_obj_add_event_cb(scr, config_screen_delete_cb, LV_EVENT_DELETE, NULL);

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

    // Tema (dropdown) — TEMA1 (original), TEMA2 (esportivo), TEMA3 (classico).
    lv_obj_t *row_tema = add_row(scroll, "Tema");
    lv_obj_t *dd_tema = lv_dropdown_create(row_tema);
    lv_dropdown_set_options(dd_tema, "TEMA1\nTEMA2\nTEMA3");
    lv_dropdown_set_selected(dd_tema, (uint16_t)zotti_theme_get());
    lv_obj_set_size(dd_tema, 200, 34);
    lv_obj_align(dd_tema, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_tema, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_tema, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_tema, ZOTTI_FONT_TINY, 0);
    lv_obj_add_event_cb(dd_tema, tema_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Brilho (slider) — esmaecimento por software, ver zotti_brightness.h
    // pra por que este hardware nao tem PWM de backlight de verdade.
    lv_obj_t *row_brilho = add_row(scroll, "Brilho");
    lv_obj_t *slider_brilho = lv_slider_create(row_brilho);
    lv_obj_set_size(slider_brilho, 150, 12);
    lv_obj_align(slider_brilho, LV_ALIGN_RIGHT_MID, -70, 0);
    lv_slider_set_range(slider_brilho, ZOTTI_BRIGHTNESS_MIN, ZOTTI_BRIGHTNESS_MAX);
    lv_slider_set_value(slider_brilho, zotti_brightness_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_brilho, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_brilho, ZOTTI_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_brilho, brilho_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_brilho = lv_label_create(row_brilho);
    lv_label_set_text_fmt(s_lbl_brilho, "%u%%", (unsigned)zotti_brightness_get());
    lv_obj_set_style_text_font(s_lbl_brilho, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_brilho, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_brilho, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *lbl_brilho_note = lv_label_create(scroll);
    lv_label_set_text(lbl_brilho_note,
        "  (esmaecimento por software — este hardware nao tem PWM de backlight real)");
    lv_obj_set_style_text_font(lbl_brilho_note, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_brilho_note, ZOTTI_GRAY, 0);

    // Secao: Dashboard — Estilo, Modelo e Corte do mostrador (antes ficava
    // numa tela de "Perfis" separada, aberta de dentro do dashboard; agora
    // e configuracao unica aqui, aplicando na hora, sem botao Salvar).
    add_section_label(scroll, "  DASHBOARD");

    app_dash_profile_t dash_p;
    get_active_dash_profile(&dash_p);

    // Estilo (Sport / Classico).
    lv_obj_t *row_estilo = add_row(scroll, "Estilo do mostrador");
    lv_obj_t *dd_estilo = lv_dropdown_create(row_estilo);
    lv_dropdown_set_options(dd_estilo, "Sport\nClassico");
    lv_dropdown_set_selected(dd_estilo, (uint16_t)dash_p.gauge_style);
    lv_obj_set_size(dd_estilo, 200, 34);
    lv_obj_align(dd_estilo, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_estilo, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_estilo, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_estilo, ZOTTI_FONT_TINY, 0);
    lv_obj_add_event_cb(dd_estilo, estilo_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Modelo (Classico / Race / Grid / Duplo) — mesma ordem de app_dash_layout_t.
    lv_obj_t *row_modelo = add_row(scroll, "Modelo da tela");
    lv_obj_t *dd_modelo = lv_dropdown_create(row_modelo);
    lv_dropdown_set_options(dd_modelo, "Classico\nRace\nGrid\nDuplo");
    lv_dropdown_set_selected(dd_modelo, (uint16_t)dash_p.layout);
    lv_obj_set_size(dd_modelo, 200, 34);
    lv_obj_align(dd_modelo, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_modelo, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_modelo, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_modelo, ZOTTI_FONT_TINY, 0);
    lv_obj_add_event_cb(dd_modelo, modelo_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Cor do acento do RPM (so aparece no layout Grid, ver ui_dash_accent_color).
    lv_obj_t *row_cor = add_row(scroll, "Cor do acento (RPM)");
    lv_obj_t *dd_cor = lv_dropdown_create(row_cor);
    lv_dropdown_set_options(dd_cor, "Azul\nVerde\nAmarelo\nVermelho\nRoxo\nBranco");
    lv_dropdown_set_selected(dd_cor, (uint16_t)(dash_p.color_theme % UI_DASH_ACCENT_COUNT));
    lv_obj_set_size(dd_cor, 200, 34);
    lv_obj_align(dd_cor, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(dd_cor, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_text_color(dd_cor, ZOTTI_WHITE, 0);
    lv_obj_set_style_text_font(dd_cor, ZOTTI_FONT_TINY, 0);
    lv_obj_add_event_cb(dd_cor, cor_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Corte (RPM) — stepper -/+.
    lv_obj_t *row_corte = add_row(scroll, "Corte (RPM)");
    lv_obj_t *btn_corte_dec = lv_btn_create(row_corte);
    lv_obj_set_size(btn_corte_dec, 40, 34);
    lv_obj_align(btn_corte_dec, LV_ALIGN_RIGHT_MID, -170, 0);
    lv_obj_set_style_bg_color(btn_corte_dec, ZOTTI_BG_HEADER, 0);
    lv_obj_add_event_cb(btn_corte_dec, corte_dec_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_corte_dec = lv_label_create(btn_corte_dec);
    lv_label_set_text(lbl_corte_dec, "-");
    lv_obj_center(lbl_corte_dec);

    s_lbl_corte = lv_label_create(row_corte);
    lv_obj_set_style_text_font(s_lbl_corte, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_corte, ZOTTI_WHITE, 0);
    lv_obj_align(s_lbl_corte, LV_ALIGN_RIGHT_MID, -75, 0);
    refresh_corte_label();

    lv_obj_t *btn_corte_inc = lv_btn_create(row_corte);
    lv_obj_set_size(btn_corte_inc, 40, 34);
    lv_obj_align(btn_corte_inc, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_corte_inc, ZOTTI_BG_HEADER, 0);
    lv_obj_add_event_cb(btn_corte_inc, corte_inc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_corte_inc = lv_label_create(btn_corte_inc);
    lv_label_set_text(lbl_corte_inc, "+");
    lv_obj_center(lbl_corte_inc);

    // Zerar minimo/maximo salvo dos sensores.
    lv_obj_t *row_minmax = add_row(scroll, LV_SYMBOL_REFRESH "  Minimo/Maximo dos sensores");
    lv_obj_t *btn_minmax = lv_btn_create(row_minmax);
    lv_obj_set_size(btn_minmax, 150, 34);
    lv_obj_align(btn_minmax, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_minmax, lv_color_hex(0x3A2000), 0);
    lv_obj_set_style_radius(btn_minmax, 4, 0);
    lv_obj_add_event_cb(btn_minmax, reset_minmax_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_minmax = lv_label_create(btn_minmax);
    lv_label_set_text(lbl_minmax, "Zerar");
    lv_obj_set_style_text_font(lbl_minmax, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_minmax);

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

    ui_screen_load(scr);
    ESP_LOGI(TAG, "Config criado");
}
