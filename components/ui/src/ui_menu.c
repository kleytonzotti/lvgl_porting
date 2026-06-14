#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_MENU";

// ── Estrutura de cada item do menu ───────────────────────────
typedef struct {
    const char *icon;
    const char *label;
    void (*action)(void);
} menu_item_t;

// ── Callbacks de navegação ────────────────────────────────────
static void cb_dashboard(lv_event_t *e)  { LV_UNUSED(e); ESP_LOGI(TAG, "-> Dashboard");   ui_screen_dashboard_show(); }
static void cb_scanner(lv_event_t *e)    { LV_UNUSED(e); ESP_LOGI(TAG, "-> Scanner");     ui_screen_scanner_show(); }
static void cb_ecu(lv_event_t *e)        { LV_UNUSED(e); ESP_LOGI(TAG, "-> ECU");         ui_screen_ecu_show(); }
static void cb_can(lv_event_t *e)        { LV_UNUSED(e); ESP_LOGI(TAG, "-> CAN");         ui_screen_can_show(); }
static void cb_datalogger(lv_event_t *e) { LV_UNUSED(e); ESP_LOGI(TAG, "-> DataLogger");  ui_screen_datalogger_show(); }
static void cb_config(lv_event_t *e)     { LV_UNUSED(e); ESP_LOGI(TAG, "-> Config");      ui_screen_config_show(); }
static void cb_sistema(lv_event_t *e)    { LV_UNUSED(e); ESP_LOGI(TAG, "-> Sistema");     ui_screen_sistema_show(); }

// ── Cria um botão do menu ─────────────────────────────────────
static void create_menu_btn(lv_obj_t *parent, const char *icon,
                             const char *label, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 170, 140);
    lv_obj_set_style_bg_color(btn, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_bg_color(btn, ZOTTI_ACCENT_DIM, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_color(btn, ZOTTI_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    // Barra colorida no topo do botão
    lv_obj_t *top_bar = lv_obj_create(btn);
    lv_obj_set_size(top_bar, 170, 4);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, ZOTTI_ACCENT, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Ícone
    lv_obj_t *lbl_icon = lv_label_create(btn);
    lv_label_set_text(lbl_icon, icon);
    lv_obj_set_style_text_font(lbl_icon, ZOTTI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(lbl_icon, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_icon, LV_ALIGN_CENTER, 0, -18);

    // Label
    lv_obj_t *lbl_text = lv_label_create(btn);
    lv_label_set_text(lbl_text, label);
    lv_obj_set_style_text_font(lbl_text, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_text, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_text, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
}

// ── Menu Principal ────────────────────────────────────────────
void ui_menu_show(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header ───────────────────────────────────────────────
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, ZOTTI_ACCENT, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "ZOTTI INFO");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *lbl_sub = lv_label_create(header);
    lv_label_set_text(lbl_sub, "Menu Principal");
    lv_obj_set_style_text_font(lbl_sub, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_sub, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_RIGHT_MID, -15, 0);

    // ── Grade de botões (flex row-wrap centrado) ──────────────
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 800, 410);
    lv_obj_set_pos(grid, 0, 55);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 15, 0);
    lv_obj_set_style_pad_column(grid, 15, 0);
    lv_obj_set_style_pad_row(grid, 15, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP, 0);
    lv_obj_set_style_flex_main_place(grid, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(grid, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(grid, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    create_menu_btn(grid, LV_SYMBOL_CHARGE,   "Dashboard",   cb_dashboard);
    create_menu_btn(grid, LV_SYMBOL_REFRESH,  "Scanner",     cb_scanner);
    create_menu_btn(grid, LV_SYMBOL_SETTINGS, "ECU",         cb_ecu);
    create_menu_btn(grid, LV_SYMBOL_LIST,     "CAN",         cb_can);
    create_menu_btn(grid, LV_SYMBOL_SAVE,     "Data Logger", cb_datalogger);
    create_menu_btn(grid, LV_SYMBOL_EDIT,     "Configurações", cb_config);
    create_menu_btn(grid, LV_SYMBOL_HOME,     "Sistema",     cb_sistema);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Menu principal exibido — 7 botoes disponiveis");
}
