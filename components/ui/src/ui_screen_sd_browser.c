#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SD";

#define MAX_ENTRIES  APP_CAN_SD_MAX_ENTRIES
#define SD_ROOT      "/sdcard"

// ─────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────
static lv_obj_t *s_scr        = NULL;
static lv_obj_t *s_list       = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_path   = NULL;
static lv_obj_t *s_lbl_back   = NULL;

static app_can_sd_entry_t s_entries[MAX_ENTRIES];
static int                s_entry_count   = 0;
static void             (*s_back_fn)(void) = NULL;
static char               s_current_path[128];

// ─────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────
static void build_list(void);
static void request_reload(void);

// ─────────────────────────────────────────────────────
// Path helpers
// ─────────────────────────────────────────────────────

static bool at_root(void)
{
    return (strcmp(s_current_path, SD_ROOT) == 0);
}

static void go_up(void)
{
    char *last = strrchr(s_current_path, '/');
    if (last && (last - s_current_path) >= (int)strlen(SD_ROOT)) {
        *last = '\0';
        if (strlen(s_current_path) < strlen(SD_ROOT)) {
            strncpy(s_current_path, SD_ROOT, sizeof(s_current_path) - 1);
        }
    }
    request_reload();
}

// ─────────────────────────────────────────────────────
// Async helpers
//
// As funções abaixo com sufixo "_from_worker" são chamadas PELA task
// dedicada de SD (app_can.c: sd_worker_task), nunca pela task do LVGL —
// por isso elas só podem fazer uma coisa seguramente: agendar o trabalho
// de verdade na tela via lv_async_call. As funções "_async" (chamadas por
// lv_async_call) são as únicas que mexem em lv_obj, e sempre checam se a
// tela ainda existe antes (o usuário pode ter saído da tela enquanto o
// pedido de SD ainda estava em andamento).
// ─────────────────────────────────────────────────────

static void apply_dir_result_async(void *arg)
{
    LV_UNUSED(arg);
    if (!s_scr) return;   // tela já foi fechada enquanto o pedido rodava

    s_entry_count = app_can_sd_async_get_dir_result(s_entries, MAX_ENTRIES);

    // Trava de segurança: s_entries[] só tem MAX_ENTRIES posições — nunca
    // iterar além disso em build_list(), não importa o que a contagem diga.
    // Sem isso, uma contagem errada vira leitura fora do array e um loop
    // de criação de lv_obj que não termina (foi o que travou antes).
    if (s_entry_count > (int)MAX_ENTRIES) {
        ESP_LOGE(TAG, "contagem de SD (%d) maior que o buffer (%d) — truncando",
                 s_entry_count, (int)MAX_ENTRIES);
        s_entry_count = (int)MAX_ENTRIES;
    }

    if (s_lbl_status) {
        if (s_entry_count < 0) {
            lv_label_set_text(s_lbl_status, "Erro");
        } else {
            lv_label_set_text_fmt(s_lbl_status, "%d item(s)", s_entry_count);
        }
    }

    if (s_list) lv_obj_clean(s_list);
    build_list();

    ESP_LOGI(TAG, "SD dir '%s': %d entries", s_current_path, s_entry_count);
}

static void on_list_done_from_worker(void)
{
    lv_async_call(apply_dir_result_async, NULL);
}

// Não bloqueia: só enfileira mount+list na task dedicada de SD e volta na
// hora. O resultado chega depois, via on_list_done_from_worker -> lv_async_call.
static void request_reload(void)
{
    if (s_lbl_status) lv_label_set_text(s_lbl_status, "Carregando...");
    if (s_lbl_path) {
        // Atualiza o caminho na hora — é só formatação de texto, não é I/O.
        const char *rel = s_current_path + strlen(SD_ROOT);
        lv_label_set_text_fmt(s_lbl_path, "SD:%s%s",
                              at_root() ? "" : rel,
                              at_root() ? "/" : "");
    }
    if (s_lbl_back) {
        lv_label_set_text(s_lbl_back,
                          at_root() ? LV_SYMBOL_LEFT " Voltar"
                                    : LV_SYMBOL_LEFT " ..");
    }

    app_can_sd_async_mount(NULL);
    app_can_sd_async_list_dir(s_current_path, on_list_done_from_worker);
}

// ─────────────────────────────────────────────────────
// Format modal
// ─────────────────────────────────────────────────────

static void apply_format_result_async(void *arg)
{
    LV_UNUSED(arg);
    if (!s_scr) return;

    esp_err_t err = app_can_sd_async_get_last_err();
    if (s_lbl_status) {
        if (err == ESP_OK) {
            lv_label_set_text(s_lbl_status, "Formatado com sucesso!");
        } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT) {
            lv_label_set_text(s_lbl_status, "Cartao nao encontrado!");
        } else {
            lv_label_set_text_fmt(s_lbl_status, "Erro: 0x%x", (unsigned)err);
        }
    }

    strncpy(s_current_path, SD_ROOT, sizeof(s_current_path) - 1);
    s_current_path[sizeof(s_current_path) - 1] = '\0';
    request_reload();
}

static void on_format_done_from_worker(void)
{
    lv_async_call(apply_format_result_async, NULL);
}

static void format_confirm_cb(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    if (overlay) lv_obj_delete(overlay);

    if (s_lbl_status) lv_label_set_text(s_lbl_status, "Formatando...");
    app_can_sd_async_format(on_format_done_from_worker);
}

static void format_cancel_cb(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    if (overlay) lv_obj_delete(overlay);
}

static void format_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_scr) return;

    // Semi-transparent full-screen overlay
    lv_obj_t *overlay = lv_obj_create(s_scr);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog box
    lv_obj_t *dlg = lv_obj_create(overlay);
    lv_obj_set_size(dlg, 420, 200);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0D1F33), 0);
    lv_obj_set_style_border_color(dlg, ZOTTI_RED, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_radius(dlg, 8, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(dlg);
    lv_label_set_text(lbl_title, LV_SYMBOL_WARNING "  FORMATAR SD?");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_RED, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *lbl_msg = lv_label_create(dlg);
    lv_label_set_text(lbl_msg, "Todos os arquivos serao apagados.\nEsta acao nao pode ser desfeita.");
    lv_obj_set_style_text_font(lbl_msg, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_msg, ZOTTI_GRAY, 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 10);

    // Cancel button
    lv_obj_t *btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, 140, 36);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 20, -14);
    lv_obj_set_style_bg_color(btn_cancel, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_radius(btn_cancel, 4, 0);
    lv_obj_add_event_cb(btn_cancel, format_cancel_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, LV_SYMBOL_CLOSE "  Cancelar");
    lv_obj_set_style_text_font(lbl_c, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_c);

    // Confirm button
    lv_obj_t *btn_ok = lv_btn_create(dlg);
    lv_obj_set_size(btn_ok, 140, 36);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -20, -14);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x7A0000), 0);
    lv_obj_set_style_radius(btn_ok, 4, 0);
    lv_obj_add_event_cb(btn_ok, format_confirm_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, LV_SYMBOL_WARNING "  Formatar");
    lv_obj_set_style_text_font(lbl_ok, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_ok);
}

// ─────────────────────────────────────────────────────
// Event callbacks
// ─────────────────────────────────────────────────────

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_scr = s_list = s_lbl_status = s_lbl_path = s_lbl_back = NULL;
    s_entry_count = 0;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (at_root()) {
        if (s_back_fn) ui_nav(s_back_fn);
    } else {
        go_up();
    }
}

static void enter_dir_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_entry_count) return;
    if (!s_entries[idx].is_dir) return;

    size_t cur_len  = strlen(s_current_path);
    size_t name_len = strlen(s_entries[idx].name);
    if (cur_len + 1 + name_len + 1 <= sizeof(s_current_path)) {
        s_current_path[cur_len] = '/';
        memcpy(s_current_path + cur_len + 1, s_entries[idx].name, name_len + 1);
    }
    request_reload();
}

static void on_delete_done_from_worker(void)
{
    lv_async_call(apply_dir_result_async, NULL);
}

static void delete_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_entry_count) return;
    if (s_entries[idx].is_dir) return;

    char full_path[172];   // 128 (path) + 1 (/) + 40 (name) + 3 pad
    snprintf(full_path, sizeof(full_path), "%s/%s", s_current_path, s_entries[idx].name);

    // O delete em si é rápido (um unlink), mas a listagem seguinte é que
    // precisa ir pela task de SD — então já reaproveita o mesmo caminho:
    // apaga de forma assíncrona e, ao terminar, pede a listagem de novo.
    app_can_sd_async_delete_file(full_path, on_delete_done_from_worker);
}

// ─────────────────────────────────────────────────────
// List & path label
// ─────────────────────────────────────────────────────

static void build_list(void)
{
    if (!s_list) return;

    if (s_entry_count <= 0) {
        lv_obj_t *lbl = lv_label_create(s_list);
        lv_label_set_text(lbl, s_entry_count < 0 ? "SD nao montado ou erro ao ler."
                                                   : "Pasta vazia.");
        lv_obj_set_style_text_color(lbl, ZOTTI_GRAY, 0);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_SMALL, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    ESP_LOGI(TAG, "[DIAG-SD-LIST] iniciando build_list: %d entradas | internal_free=%uB",
             s_entry_count, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    for (int i = 0; i < s_entry_count; i++) {
        bool is_dir = s_entries[i].is_dir;

        ESP_LOGI(TAG, "[DIAG-SD-LIST] linha %d/%d (%s) | internal_free=%uB",
                 i, s_entry_count, s_entries[i].name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, 760, 44);
        lv_obj_set_style_bg_color(row,
            (i % 2 == 0) ? ZOTTI_BG_CARD : lv_color_hex(0x06101C), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text_fmt(lbl_name,
            is_dir ? LV_SYMBOL_DIRECTORY "  %s" : LV_SYMBOL_FILE "  %s",
            s_entries[i].name);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_name,
            is_dir ? ZOTTI_ACCENT : lv_color_hex(0xBDEEFF), 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 12, 0);

        if (is_dir) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, enter_dir_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            lv_obj_t *lbl_arr = lv_label_create(row);
            lv_label_set_text(lbl_arr, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_font(lbl_arr, ZOTTI_FONT_SMALL, 0);
            lv_obj_set_style_text_color(lbl_arr, ZOTTI_GRAY, 0);
            lv_obj_align(lbl_arr, LV_ALIGN_RIGHT_MID, -12, 0);
        } else {
            lv_obj_t *lbl_size = lv_label_create(row);
            if (s_entries[i].size_kb == 0) {
                lv_label_set_text(lbl_size, "< 1 KB");
            } else {
                lv_label_set_text_fmt(lbl_size, "%lu KB",
                                      (unsigned long)s_entries[i].size_kb);
            }
            lv_obj_set_style_text_font(lbl_size, ZOTTI_FONT_TINY, 0);
            lv_obj_set_style_text_color(lbl_size, ZOTTI_GRAY, 0);
            lv_obj_align(lbl_size, LV_ALIGN_RIGHT_MID, -110, 0);

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
}

// ─────────────────────────────────────────────────────
// Screen creation
// ─────────────────────────────────────────────────────

void ui_screen_sd_browser_show(void (*back_fn)(void))
{
    s_back_fn = back_fn ? back_fn : ui_menu_show;
    strncpy(s_current_path, SD_ROOT, sizeof(s_current_path) - 1);
    s_current_path[sizeof(s_current_path) - 1] = '\0';
    s_entry_count = 0;

    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
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

    // Back / Up button
    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, LV_SYMBOL_LEFT " Voltar");
    lv_obj_set_style_text_font(s_lbl_back, ZOTTI_FONT_TINY, 0);
    lv_obj_center(s_lbl_back);

    // Title
    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, LV_SYMBOL_DIRECTORY "  ARQUIVOS SD");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, -50, 0);

    // Format button (right side)
    lv_obj_t *btn_fmt = lv_btn_create(hdr);
    lv_obj_set_size(btn_fmt, 130, 28);
    lv_obj_align(btn_fmt, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(btn_fmt, lv_color_hex(0x5A0A0A), 0);
    lv_obj_set_style_radius(btn_fmt, 4, 0);
    lv_obj_add_event_cb(btn_fmt, format_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_fmt = lv_label_create(btn_fmt);
    lv_label_set_text(lbl_fmt, LV_SYMBOL_WARNING " Formatar SD");
    lv_obj_set_style_text_font(lbl_fmt, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_fmt);

    // ── Status bar (path + count) ─────────────────────
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 30);
    lv_obj_set_pos(status_bar, 0, 42);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_path = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_path, "SD:/");
    lv_obj_set_style_text_font(s_lbl_path, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_path, ZOTTI_ACCENT, 0);
    lv_obj_align(s_lbl_path, LV_ALIGN_LEFT_MID, 12, 0);

    s_lbl_status = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_status, "Carregando...");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_RIGHT_MID, -12, 0);

    // ── File/folder list (scrollable) ────────────────
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

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    // Não bloqueia: mount+list são enfileirados na task de SD e a tela já
    // aparece na hora, mostrando "Carregando..." até o resultado chegar.
    request_reload();
}
