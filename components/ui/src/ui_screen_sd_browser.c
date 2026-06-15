#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SD";

#define MAX_FILES 32

// ─────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────
static lv_obj_t *s_list       = NULL;
static lv_obj_t *s_lbl_empty  = NULL;
static lv_obj_t *s_lbl_status = NULL;

static app_can_sd_file_t s_files[MAX_FILES];
static int                s_file_count = 0;
static void             (*s_back_fn)(void) = NULL;

// ─────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────

static void build_list(void);

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_list = s_lbl_empty = s_lbl_status = NULL;
    s_file_count = 0;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_back_fn) ui_nav(s_back_fn);
}

static void delete_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_file_count) return;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/sdcard/%s", s_files[idx].name);
    bool ok = app_can_sd_delete_file(full_path);
    ESP_LOGI(TAG, "Delete %s: %s", full_path, ok ? "OK" : "FAIL");

    // Refresh list
    s_file_count = app_can_sd_list_csv(s_files, MAX_FILES);
    if (s_list) {
        lv_obj_clean(s_list);
    }
    build_list();
}

static void build_list(void)
{
    if (!s_list) return;

    if (s_lbl_status) {
        if (s_file_count < 0) {
            lv_label_set_text(s_lbl_status, "SD nao montado ou erro ao ler.");
        } else {
            lv_label_set_text_fmt(s_lbl_status,
                "%d arquivo(s) CSV  |  /sdcard/", s_file_count);
        }
    }

    if (s_file_count <= 0) {
        if (s_lbl_empty) lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_lbl_empty) lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < s_file_count; i++) {
        // Row container
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, 760, 44);
        lv_obj_set_style_bg_color(row, (i % 2 == 0) ? ZOTTI_BG_CARD : lv_color_hex(0x06101C), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // File icon + name
        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text_fmt(lbl_name, LV_SYMBOL_FILE "  %s", s_files[i].name);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xBDEEFF), 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 12, 0);

        // Size
        lv_obj_t *lbl_size = lv_label_create(row);
        if (s_files[i].size_kb == 0) {
            lv_label_set_text(lbl_size, "< 1 KB");
        } else {
            lv_label_set_text_fmt(lbl_size, "%lu KB", (unsigned long)s_files[i].size_kb);
        }
        lv_obj_set_style_text_font(lbl_size, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_size, ZOTTI_GRAY, 0);
        lv_obj_align(lbl_size, LV_ALIGN_RIGHT_MID, -110, 0);

        // Delete button
        lv_obj_t *btn_del = lv_btn_create(row);
        lv_obj_set_size(btn_del, 90, 30);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x4A0000), 0);
        lv_obj_set_style_radius(btn_del, 4, 0);
        lv_obj_add_event_cb(btn_del, delete_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH " DEL");
        lv_obj_set_style_text_font(lbl_del, ZOTTI_FONT_TINY, 0);
        lv_obj_center(lbl_del);
    }
}

// ─────────────────────────────────────────────────────
// Screen creation
// ─────────────────────────────────────────────────────

void ui_screen_sd_browser_show(void (*back_fn)(void))
{
    s_back_fn    = back_fn ? back_fn : ui_menu_show;
    s_file_count = 0;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    // ── Header ───────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 800, 42);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Voltar");
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, LV_SYMBOL_SAVE "  ARQUIVOS SD");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Status bar ───────────────────────────────────
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 30);
    lv_obj_set_pos(status_bar, 0, 42);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_status, "Carregando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 12, 0);

    // ── File list (scrollable) ────────────────────────
    lv_obj_t *list_cont = lv_obj_create(scr);
    lv_obj_set_size(list_cont, 800, 408);
    lv_obj_set_pos(list_cont, 0, 72);
    lv_obj_set_style_bg_color(list_cont, ZOTTI_BG, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 20, 0);
    lv_obj_set_style_pad_row(list_cont, 4, 0);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_style_radius(list_cont, 0, 0);

    s_list = list_cont;

    // Empty state label
    s_lbl_empty = lv_label_create(list_cont);
    lv_label_set_text(s_lbl_empty, "Nenhum arquivo .csv encontrado no SD.\n\n"
                                   "Inicie o Sniffer para gravar dados CAN.");
    lv_obj_set_style_text_color(s_lbl_empty, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_font(s_lbl_empty, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(s_lbl_empty);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    // Load file list (mount SD first if not already mounted)
    app_can_sd_mount();
    s_file_count = app_can_sd_list_csv(s_files, MAX_FILES);
    build_list();

    ESP_LOGI(TAG, "SD browser: %d files", s_file_count);
}
