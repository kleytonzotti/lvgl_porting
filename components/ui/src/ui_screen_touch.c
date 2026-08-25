#include "ui.h"
#include "esp_log.h"
#include "bsp_waveshare_43.h"

static const char *TAG = "UI_TOUCH";

static lv_obj_t *s_label_coords = NULL;
static lv_obj_t *s_canvas       = NULL;
static lv_obj_t *s_label_status = NULL;
static lv_obj_t *s_touch_point = NULL;


// Area de desenho do ponto.
static void draw_touch_point(lv_coord_t x, lv_coord_t y)
{
    if (!s_touch_point) {
        s_touch_point = lv_obj_create(s_canvas);
        lv_obj_set_size(s_touch_point, 20, 20);
        lv_obj_set_style_radius(s_touch_point, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_touch_point, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_border_width(s_touch_point, 0, 0);
    }

    lv_obj_set_pos(s_touch_point, x - 10, y - 10);
    lv_obj_clear_flag(s_touch_point, LV_OBJ_FLAG_HIDDEN);
}

// Callback chamado pela task de touch.
void ui_screen_touch_update(lv_coord_t x, lv_coord_t y, bool pressed)
{
    if (!s_label_coords || !s_label_status || !s_canvas) return;

    // Log para diagnostico: mostra coordenada raw e dimensao do canvas.
    lv_coord_t cx = lv_obj_get_x(s_canvas);
    lv_coord_t cy = lv_obj_get_y(s_canvas);
    lv_coord_t cw = lv_obj_get_width(s_canvas);
    lv_coord_t ch = lv_obj_get_height(s_canvas);

    if (pressed) {
        ESP_LOGI("TOUCH_DBG",
            "RAW x=%ld y=%ld | canvas pos=(%ld,%ld) size=%ldx%ld",
            (long)x, (long)y, (long)cx, (long)cy, (long)cw, (long)ch);

        lv_label_set_text_fmt(s_label_coords, "X: %ld   Y: %ld", (long)x, (long)y);
        lv_label_set_text(s_label_status, "Estado: PRESSIONADO");
        lv_obj_set_style_text_color(s_label_status,
            lv_palette_main(LV_PALETTE_GREEN), 0);
        draw_touch_point(x, y);
    } else {
        ESP_LOGI("TOUCH_DBG", "SOLTO");
        lv_label_set_text(s_label_status, "Estado: SOLTO");
        lv_obj_set_style_text_color(s_label_status,
            lv_palette_main(LV_PALETTE_GREY), 0);
        if (s_touch_point) lv_obj_add_flag(s_touch_point, LV_OBJ_FLAG_HIDDEN);
    }
}

// Criacao da tela.
void ui_screen_touch_create(lv_obj_t *parent)
{
    // Titulo.
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Diagnostico de Touch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    // Coordenadas
    s_label_coords = lv_label_create(parent);
    lv_label_set_text(s_label_coords, "X: ---   Y: ---");
    lv_obj_set_style_text_font(s_label_coords, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label_coords, LV_ALIGN_TOP_LEFT, 10, 35);

    // Status
    s_label_status = lv_label_create(parent);
    lv_label_set_text(s_label_status, "Estado: SOLTO");
    lv_obj_set_style_text_color(s_label_status,
        lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(s_label_status, LV_ALIGN_TOP_LEFT, 10, 60);

    // Area de toque: canvas onde o ponto aparece.
    s_canvas = lv_obj_create(parent);
    lv_obj_set_size(s_canvas, 780, 340);
    lv_obj_align(s_canvas, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(s_canvas, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_color(s_canvas,
        lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_set_style_border_width(s_canvas, 1, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    // Instrucoes no centro do canvas.
    lv_obj_t *hint = lv_label_create(s_canvas);
    lv_label_set_text(hint, "Toque na tela para ver o ponto");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    ESP_LOGI(TAG, "Tela touch criada");
    bsp_touch_register_cb(ui_screen_touch_update);
}
