#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

#define SPLASH_TIME_MS   5000

static const char *TAG = "UI_SPLASH";

// ── Callback de animação da barra ──────────────────────────────
static void bar_anim_cb(void *bar, int32_t val)
{
    lv_bar_set_value((lv_obj_t *)bar, val, LV_ANIM_OFF);
}

// ── Transição para o menu ─────────────────────────────────────
static void splash_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_menu_show();
}

// ── Cria linha decorativa ─────────────────────────────────────
static lv_obj_t *create_deco_line(lv_obj_t *parent, int32_t w, int32_t y_ofs)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, w, 2);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, y_ofs);
    lv_obj_set_style_bg_color(line, ZOTTI_ACCENT, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 1, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

// ── Cria ponto decorativo nas extremidades da linha ──────────
static void create_deco_dot(lv_obj_t *parent, lv_align_t align, int32_t x, int32_t y)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_align(dot, align, x, y);
    lv_obj_set_style_bg_color(dot, ZOTTI_ACCENT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
}

// ── Splash Screen ─────────────────────────────────────────────
void ui_splash_show(void)
{
    // Tela dedicada
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Painel central
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 700, 300);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Linha superior decorativa
    create_deco_line(panel, 500, -90);
    create_deco_dot(panel, LV_ALIGN_CENTER, -250, -90);
    create_deco_dot(panel, LV_ALIGN_CENTER,  250, -90);

    // Texto "ZOTTI" (branco)
    lv_obj_t *lbl_zotti = lv_label_create(panel);
    lv_label_set_text(lbl_zotti, "ZOTTI");
    lv_obj_set_style_text_font(lbl_zotti, ZOTTI_FONT_LOGO, 0);
    lv_obj_set_style_text_color(lbl_zotti, ZOTTI_WHITE, 0);
    lv_obj_align(lbl_zotti, LV_ALIGN_CENTER, -110, -20);

    // Texto "INFO" (ciano)
    lv_obj_t *lbl_info = lv_label_create(panel);
    lv_label_set_text(lbl_info, "INFO");
    lv_obj_set_style_text_font(lbl_info, ZOTTI_FONT_LOGO, 0);
    lv_obj_set_style_text_color(lbl_info, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_info, LV_ALIGN_CENTER, 100, -20);

    // Linha separadora
    lv_obj_t *sep = lv_obj_create(panel);
    lv_obj_set_size(sep, 400, 1);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(sep, ZOTTI_GRAY_DARK, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Tagline
    lv_obj_t *tagline = lv_label_create(panel);
    lv_label_set_text(tagline, "Automotive Intelligence");
    lv_obj_set_style_text_font(tagline, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(tagline, ZOTTI_GRAY, 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 65);

    // Linha inferior decorativa
    create_deco_line(panel, 500, 110);
    create_deco_dot(panel, LV_ALIGN_CENTER, -250, 110);
    create_deco_dot(panel, LV_ALIGN_CENTER,  250, 110);

    // Versão (canto inferior direito da tela)
    lv_obj_t *lbl_ver = lv_label_create(scr);
    lv_label_set_text(lbl_ver, "v1.0");
    lv_obj_set_style_text_font(lbl_ver, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_ver, ZOTTI_GRAY_DARK, 0);
    lv_obj_align(lbl_ver, LV_ALIGN_BOTTOM_RIGHT, -15, -10);

    // Barra de loading na parte inferior
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 400, 4);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(bar, ZOTTI_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, ZOTTI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Anima a barra de 0 a 100 em SPLASH_TIME_MS
    lv_anim_t a_bar;
    lv_anim_init(&a_bar);
    lv_anim_set_var(&a_bar, bar);
    lv_anim_set_exec_cb(&a_bar, bar_anim_cb);
    lv_anim_set_values(&a_bar, 0, 100);
    lv_anim_set_duration(&a_bar, SPLASH_TIME_MS - 200);
    lv_anim_start(&a_bar);

    // Carrega a tela imediatamente (sem animação pesada de FADE)
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    // Timer único para ir ao menu
    lv_timer_t *t = lv_timer_create(splash_timer_cb, SPLASH_TIME_MS, NULL);
    lv_timer_set_repeat_count(t, 1);

    ESP_LOGI(TAG, "Splash iniciado");
}

// Mantida por compatibilidade — apenas delega
void ui_splash_create(void)
{
    ui_splash_show();
}
