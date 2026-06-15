#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_DASHBOARD";

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

// Atualiza os dados da dashboard.
void ui_screen_dashboard_update(int32_t rpm, int32_t speed_kph,
                                 int32_t map_kpa, int32_t tps_pct,
                                 float afr, int32_t ect_c,
                                 int32_t iat_c, float batt_v)
{
    if (!s_arc_rpm) return;

    lv_arc_set_value(s_arc_rpm, rpm);
    lv_label_set_text_fmt(s_lbl_rpm,   "%ld", (long)rpm);
    lv_label_set_text_fmt(s_lbl_speed, "%ld", (long)speed_kph);
    lv_bar_set_value(s_bar_tps, tps_pct, LV_ANIM_ON);
    lv_label_set_text_fmt(s_lbl_map,  "%ld kPa", (long)map_kpa);
    lv_label_set_text_fmt(s_lbl_tps,  "%ld%%", (long)tps_pct);
    lv_label_set_text_fmt(s_lbl_afr,  "%.1f", afr);
    lv_label_set_text_fmt(s_lbl_ect,  "%ld C", (long)ect_c);
    lv_label_set_text_fmt(s_lbl_iat,  "%ld C", (long)iat_c);
    lv_label_set_text_fmt(s_lbl_batt, "%.1fV", batt_v);

    // Cor do ECT: verde < 90 C, amarelo 90-105 C, vermelho > 105 C.
    lv_color_t ect_color = (ect_c < 90)  ? ZOTTI_GREEN  :
                           (ect_c < 105) ? ZOTTI_YELLOW : ZOTTI_RED;
    lv_obj_set_style_text_color(s_lbl_ect, ect_color, 0);

    // Cor do AFR: verde se proximo de 14.7.
    lv_color_t afr_color = (afr > 14.0f && afr < 15.4f) ? ZOTTI_GREEN : ZOTTI_YELLOW;
    lv_obj_set_style_text_color(s_lbl_afr, afr_color, 0);
}

// Card de sensor (lado direito).
static lv_obj_t *create_sensor_card(lv_obj_t *parent, const char *label_text,
                                     int32_t y_pos)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 260, 55);
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

// Callback voltar ao menu.
static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

// Dashboard.
void ui_screen_dashboard_show(void)
{
    s_arc_rpm   = NULL;
    s_lbl_rpm   = NULL;
    s_lbl_speed = NULL;

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
    // Oculta knob interativo
    lv_obj_set_style_opa(s_arc_rpm, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_rpm, LV_OBJ_FLAG_CLICKABLE);

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

    lv_obj_t *lbl_status = lv_label_create(col_mid);
    lv_label_set_text(lbl_status, LV_SYMBOL_BLUETOOTH " ECU: Aguardando...");
    lv_obj_set_style_text_font(lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);

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

    s_lbl_tps  = create_sensor_card(col_right, "TPS",        70);
    s_lbl_afr  = create_sensor_card(col_right, "AFR",       135);
    s_lbl_ect  = create_sensor_card(col_right, "ECT",       200);
    s_lbl_iat  = create_sensor_card(col_right, "IAT",       265);
    s_lbl_batt = create_sensor_card(col_right, "BATERIA",   330);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Dashboard criado");
}
