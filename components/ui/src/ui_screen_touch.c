#include "ui.h"
#include "esp_log.h"

static const char *TAG = "UI_TOUCH";

static lv_obj_t *s_label_coords = NULL;
static lv_obj_t *s_canvas       = NULL;
static lv_obj_t *s_label_status = NULL;

// ── Área de desenho do ponto ──────────────────────────────
static void draw_touch_point(lv_coord_t x, lv_coord_t y)
{
    lv_obj_clean(s_canvas);

    // Círculo no ponto tocado
    lv_obj_t *point = lv_obj_create(s_canvas);
    lv_obj_set_size(point, 20, 20);
    lv_obj_set_style_radius(point, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(point, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(point, 0, 0);
    lv_obj_set_pos(point, x - 10, y - 10);
}

// ── Callback chamado pela task de touch ──────────────────
void ui_screen_touch_update(lv_coord_t x, lv_coord_t y, bool pressed)
{
    if (!s_label_coords || !s_label_status) return;

    if (pressed) {
        lv_label_set_text_fmt(s_label_coords, "X: %d   Y: %d", x, y);
        lv_label_set_text(s_label_status, "Estado: PRESSIONADO");
        lv_obj_set_style_text_color(s_label_status,
            lv_palette_main(LV_PALETTE_GREEN), 0);
        draw_touch_point(x, y);
    } else {
        lv_label_set_text(s_label_status, "Estado: SOLTO");
        lv_obj_set_style_text_color(s_label_status,
            lv_palette_main(LV_PALETTE_GREY), 0);
    }
}

// ── Criação da tela ───────────────────────────────────────
void ui_screen_touch_create(lv_obj_t *parent)
{
    // Título
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Diagnóstico de Touch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    // Coordenadas
    s_label_coords = lv_label_create(parent);
    lv_label_set_text(s_label_coords, "X: ---   Y: ---");
    lv_obj_set_style_text_font(s_label_coords, &lv_font_montserrat_16, 0);
    lv_obj_align(s_label_coords, LV_ALIGN_TOP_LEFT, 10, 35);

    // Status
    s_label_status = lv_label_create(parent);
    lv_label_set_text(s_label_status, "Estado: SOLTO");
    lv_obj_set_style_text_color(s_label_status,
        lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(s_label_status, LV_ALIGN_TOP_LEFT, 10, 60);

    // Área de toque — canvas onde o ponto aparece
    s_canvas = lv_obj_create(parent);
    lv_obj_set_size(s_canvas, 780, 340);
    lv_obj_align(s_canvas, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(s_canvas, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_color(s_canvas,
        lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_set_style_border_width(s_canvas, 1, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    // Instruções no centro do canvas
    lv_obj_t *hint = lv_label_create(s_canvas);
    lv_label_set_text(hint, "Toque na tela para ver o ponto");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    ESP_LOGI(TAG, "Tela touch criada");
}