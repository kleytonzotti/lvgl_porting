#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "app_pedal_link.h"

static const char *TAG = "UI_PEDAL";

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

// ─────────────────────────────────────────────────────
// Modos — nomes exibidos na tela, na mesma ordem de app_pedal_mode_t.
// ─────────────────────────────────────────────────────
static const char *k_mode_names[] = { "Economia", "Normal", "Sport" };
#define MODE_COUNT (int)(sizeof(k_mode_names) / sizeof(k_mode_names[0]))

// ─────────────────────────────────────────────────────
// Estado da tela
// ─────────────────────────────────────────────────────
static lv_obj_t   *s_lbl_link      = NULL;
static lv_obj_t   *s_lbl_pedal_pct = NULL;
static lv_obj_t   *s_lbl_output_pct= NULL;
static lv_obj_t   *s_lbl_fault     = NULL;
static lv_obj_t   *s_lbl_frames    = NULL;
static lv_obj_t   *s_mode_btn[MODE_COUNT];
static lv_timer_t *s_timer         = NULL;
static app_pedal_mode_t s_selected_mode = APP_PEDAL_MODE_NORMAL;

static void highlight_selected_mode(void)
{
    for (int i = 0; i < MODE_COUNT; i++) {
        if (!s_mode_btn[i]) continue;
        lv_obj_set_style_bg_color(s_mode_btn[i],
            (i == (int)s_selected_mode) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_CARD, 0);
    }
}

static void mode_btn_cb(lv_event_t *e)
{
    app_pedal_mode_t mode = (app_pedal_mode_t)(intptr_t)lv_event_get_user_data(e);
    s_selected_mode = mode;
    app_pedal_link_set_mode(mode);
    highlight_selected_mode();
    ESP_LOGI(TAG, "Modo selecionado: %s", k_mode_names[mode]);
}

// Chamado a cada 300ms — só lê o snapshot já pronto (mutex + memcpy) e
// formata texto, mesmo padrao de ui_screen_ecu.c. Sem init() (§5 do
// ROADMAP), link_ok fica sempre falso — mostra "DESCONECTADO" corretamente
// em vez de travar ou mentir dado.
static void update_values(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    app_pedal_status_t st;
    app_pedal_link_get_status(&st);

    if (s_lbl_link) {
        lv_label_set_text(s_lbl_link, st.link_ok
            ? LV_SYMBOL_OK "  MODULO DE PEDAL: CONECTADO"
            : LV_SYMBOL_CLOSE "  MODULO DE PEDAL: DESCONECTADO");
        lv_obj_set_style_text_color(s_lbl_link, st.link_ok ? ZOTTI_GREEN : ZOTTI_RED, 0);
    }
    if (s_lbl_pedal_pct)  lv_label_set_text_fmt(s_lbl_pedal_pct,  "%u%%", (unsigned)st.pedal_pct);
    if (s_lbl_output_pct) lv_label_set_text_fmt(s_lbl_output_pct, "%u%%", (unsigned)st.output_pct);
    if (s_lbl_fault) {
        if (st.fault_flags == 0) {
            lv_label_set_text(s_lbl_fault, "Nenhuma");
            lv_obj_set_style_text_color(s_lbl_fault, ZOTTI_GREEN, 0);
        } else {
            lv_label_set_text_fmt(s_lbl_fault, "0x%02X", (unsigned)st.fault_flags);
            lv_obj_set_style_text_color(s_lbl_fault, ZOTTI_RED, 0);
        }
    }
    if (s_lbl_frames) {
        lv_label_set_text_fmt(s_lbl_frames, "OK:%lu  Chk.invalido:%lu",
            (unsigned long)st.frames_ok, (unsigned long)st.frames_bad_checksum);
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_lbl_link = s_lbl_pedal_pct = s_lbl_output_pct = s_lbl_fault = s_lbl_frames = NULL;
    for (int i = 0; i < MODE_COUNT; i++) s_mode_btn[i] = NULL;
}

// Card de leitura (usado para Pedal/Saida).
static lv_obj_t *create_reading_card(lv_obj_t *parent, const char *label_text, int32_t x)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 240, 100);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_bg_color(card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(card, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, label_text);
    lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 12, 10);

    lv_obj_t *lbl_val = lv_label_create(card);
    lv_label_set_text(lbl_val, "---");
    lv_obj_set_style_text_font(lbl_val, ZOTTI_FONT_HUGE, 0);
    lv_obj_set_style_text_color(lbl_val, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_val, LV_ALIGN_CENTER, 0, 10);

    return lbl_val;
}

// s_selected_mode persiste entre entradas na tela (mesma ideia do filtro em
// ui_screen_can_sniffer.c) — reabrir a tela nao deve resetar o modo nem
// mandar um comando novo sem o usuario tocar em nada.
void ui_screen_pedal_show(void)
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
    lv_label_set_text(lbl_title, "MODULO DE PEDAL");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Status do link.
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 36);
    lv_obj_set_pos(status_bar, 0, 40);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_link = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_link, LV_SYMBOL_CLOSE "  MODULO DE PEDAL: DESCONECTADO");
    lv_obj_set_style_text_font(s_lbl_link, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_link, ZOTTI_RED, 0);
    lv_obj_align(s_lbl_link, LV_ALIGN_LEFT_MID, 15, 0);

    // Aviso: link UART desligado de proposito (conflito de pino, ver ROADMAP.md SS5).
    lv_obj_t *lbl_warn = lv_label_create(status_bar);
    lv_label_set_text(lbl_warn, LV_SYMBOL_WARNING " UART desligada — conflito de pino (ROADMAP.md SS5)");
    lv_obj_set_style_text_font(lbl_warn, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_warn, ZOTTI_YELLOW, 0);
    lv_obj_align(lbl_warn, LV_ALIGN_RIGHT_MID, -15, 0);

    // Leituras: pedal real x saida enviada a ECU.
    lv_obj_t *readings = lv_obj_create(scr);
    lv_obj_set_size(readings, 800, 100);
    lv_obj_set_pos(readings, 0, 90);
    lv_obj_set_style_bg_opa(readings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(readings, 0, 0);
    lv_obj_set_style_pad_all(readings, 15, 0);
    lv_obj_clear_flag(readings, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_pedal_pct  = create_reading_card(readings, "PEDAL (real)",       0);
    s_lbl_output_pct = create_reading_card(readings, "SAIDA (p/ ECU)",   260);

    // Card de falhas/frames.
    lv_obj_t *fault_card = lv_obj_create(readings);
    lv_obj_set_size(fault_card, 240, 100);
    lv_obj_set_pos(fault_card, 520, 0);
    lv_obj_set_style_bg_color(fault_card, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_color(fault_card, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(fault_card, 1, 0);
    lv_obj_set_style_radius(fault_card, 8, 0);
    lv_obj_clear_flag(fault_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_fault_name = lv_label_create(fault_card);
    lv_label_set_text(lbl_fault_name, "FALHAS");
    lv_obj_set_style_text_font(lbl_fault_name, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_fault_name, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_fault_name, LV_ALIGN_TOP_LEFT, 12, 10);

    s_lbl_fault = lv_label_create(fault_card);
    lv_label_set_text(s_lbl_fault, "Nenhuma");
    lv_obj_set_style_text_font(s_lbl_fault, ZOTTI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_lbl_fault, ZOTTI_GREEN, 0);
    lv_obj_align(s_lbl_fault, LV_ALIGN_CENTER, 0, 5);

    s_lbl_frames = lv_label_create(fault_card);
    lv_label_set_text(s_lbl_frames, "OK:0  Chk.invalido:0");
    lv_obj_set_style_text_font(s_lbl_frames, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_frames, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_frames, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Selecao de modo.
    lv_obj_t *lbl_mode_title = lv_label_create(scr);
    lv_label_set_text(lbl_mode_title, "MODO DE OPERACAO");
    lv_obj_set_style_text_font(lbl_mode_title, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_mode_title, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_mode_title, LV_ALIGN_TOP_LEFT, 20, 210);

    lv_obj_t *mode_bar = lv_obj_create(scr);
    lv_obj_set_size(mode_bar, 760, 90);
    lv_obj_align(mode_bar, LV_ALIGN_TOP_MID, 0, 235);
    lv_obj_set_style_bg_opa(mode_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_bar, 0, 0);
    lv_obj_set_layout(mode_bar, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(mode_bar, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_pad_column(mode_bar, 15, 0);
    lv_obj_set_style_flex_main_place(mode_bar, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_clear_flag(mode_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MODE_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(mode_bar);
        lv_obj_set_size(btn, 230, 80);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_add_event_cb(btn, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_mode_names[i]);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_MEDIUM, 0);
        lv_obj_center(lbl);
        s_mode_btn[i] = btn;
    }
    highlight_selected_mode();

    // Nota sobre o failsafe — reforca a regra de ouro do ROADMAP.md.
    lv_obj_t *lbl_note = lv_label_create(scr);
    lv_label_set_text(lbl_note,
        "O modulo de pedal decide sozinho entrar em passthrough se o heartbeat parar\n"
        "(failsafe em hardware, nao depende desta tela).");
    lv_obj_set_style_text_font(lbl_note, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_note, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_note, LV_ALIGN_BOTTOM_MID, 0, -15);

    s_timer = lv_timer_create(update_values, 300, NULL);
    update_values(NULL);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Tela do modulo de pedal criada");
}
