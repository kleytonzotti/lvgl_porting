#include "ui.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "logo_zotti_white.h"
#include "esp_log.h"

#define SPLASH_DURATION_MS  2500

static const char *TAG = "UI_SPLASH";

static lv_obj_t *s_bar = NULL;

// Callbacks de animacao.
static void anim_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void bar_anim_cb(void *bar, int32_t val)
{
    lv_bar_set_value((lv_obj_t *)bar, val, LV_ANIM_OFF);
}

// Transicao adiada para o menu.
static void splash_to_menu_async(void *d)
{
    LV_UNUSED(d);
    s_bar = NULL;
    ui_menu_show();
}

static void splash_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    lv_async_call(splash_to_menu_async, NULL);
}

// Fundo com trilhas de circuito.
static void add_circuit_dot(lv_obj_t *parent, int x, int y, int size, lv_opa_t opa)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x22D8FF), 0);
    lv_obj_set_style_bg_opa(dot, opa, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void add_circuit_line(lv_obj_t *parent,
                             const lv_point_precise_t *pts, uint16_t n,
                             lv_opa_t opa)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, n);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0x22D8FF), 0);
    lv_obj_set_style_line_opa(line, opa, 0);
}

static void create_circuit_background(lv_obj_t *parent)
{
    static const lv_point_precise_t p0[] = {{20,360},{120,360},{170,310},{260,310}};
    static const lv_point_precise_t p1[] = {{40,420},{210,420},{270,380},{420,380}};
    static const lv_point_precise_t p2[] = {{520,95},{590,95},{640,140},{760,140}};
    static const lv_point_precise_t p3[] = {{610,410},{610,330},{690,330},{760,260}};
    static const lv_point_precise_t p4[] = {{80,80},{160,80},{220,135},{350,135}};
    static const lv_point_precise_t p5[] = {{430,435},{510,435},{555,390},{555,310}};
    static const lv_point_precise_t p6[] = {{690,60},{720,90},{720,180},{780,220}};
    static const lv_point_precise_t p7[] = {{310,55},{380,55},{430,100},{500,100}};

    add_circuit_line(parent, p0, 4, LV_OPA_40);
    add_circuit_line(parent, p1, 4, LV_OPA_20);
    add_circuit_line(parent, p2, 4, LV_OPA_40);
    add_circuit_line(parent, p3, 4, LV_OPA_20);
    add_circuit_line(parent, p4, 4, LV_OPA_20);
    add_circuit_line(parent, p5, 4, LV_OPA_20);
    add_circuit_line(parent, p6, 4, LV_OPA_20);
    add_circuit_line(parent, p7, 4, LV_OPA_20);

    add_circuit_dot(parent, 112, 354, 10, LV_OPA_70);
    add_circuit_dot(parent, 257, 305, 12, LV_OPA_70);
    add_circuit_dot(parent, 415, 374, 11, LV_OPA_60);
    add_circuit_dot(parent, 585,  89, 10, LV_OPA_70);
    add_circuit_dot(parent, 752, 134, 12, LV_OPA_60);
    add_circuit_dot(parent, 684, 324, 10, LV_OPA_60);
    add_circuit_dot(parent, 340, 129,  9, LV_OPA_60);
    add_circuit_dot(parent, 552, 306, 10, LV_OPA_60);
    add_circuit_dot(parent, 720, 176,  9, LV_OPA_50);
    add_circuit_dot(parent, 497,  94, 10, LV_OPA_50);
}

// Splash screen.
void ui_splash_show(void)
{
    ESP_LOGI(TAG, "Criando splash screen");

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Fundo azul escuro com gradiente horizontal.
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x061B35), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x0B4A7A), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Trilhas de circuito
    create_circuit_background(scr);

    // Logo ZOTTI INFO.
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &logo_zotti_white);
    lv_obj_set_style_image_recolor(logo, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(logo, LV_OPA_COVER, 0);
    lv_image_set_scale(logo, 256);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);

    // Tagline abaixo da logo.
    lv_obj_t *tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Automotive Intelligence Platform");
    lv_obj_set_style_text_font(tagline, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(tagline, lv_color_hex(0xBDEEFF), 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);

    lv_anim_t a_tag;
    lv_anim_init(&a_tag);
    lv_anim_set_var(&a_tag, tagline);
    lv_anim_set_values(&a_tag, 0, 255);
    lv_anim_set_duration(&a_tag, 1000);
    lv_anim_set_delay(&a_tag, 900);
    lv_anim_set_exec_cb(&a_tag, anim_opa_cb);
    lv_anim_start(&a_tag);

    // Barra de loading.
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 420, 8);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 140);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0A2543), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x22D8FF), LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    lv_anim_t a_bar;
    lv_anim_init(&a_bar);
    lv_anim_set_var(&a_bar, s_bar);
    lv_anim_set_exec_cb(&a_bar, bar_anim_cb);
    lv_anim_set_values(&a_bar, 0, 100);
    lv_anim_set_duration(&a_bar, SPLASH_DURATION_MS - 300);
    lv_anim_set_delay(&a_bar, 200);
    lv_anim_start(&a_bar);

    // Versao no canto inferior direito.
    lv_obj_t *lbl_ver = lv_label_create(scr);
    lv_label_set_text(lbl_ver, "v1.0");
    lv_obj_set_style_text_font(lbl_ver, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_ver, lv_color_hex(0x1A3A5A), 0);
    lv_obj_align(lbl_ver, LV_ALIGN_BOTTOM_RIGHT, -15, -10);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    lv_timer_t *t = lv_timer_create(splash_timer_cb, SPLASH_DURATION_MS, NULL);
    lv_timer_set_repeat_count(t, 1);

    ESP_LOGI(TAG, "Splash criado - %dms para o menu", SPLASH_DURATION_MS);
}
