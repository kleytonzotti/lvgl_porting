#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UI_SNIFF";

// ─────────────────────────────────────────────────────
// Preset filters: {label, id_min, id_max}
// ─────────────────────────────────────────────────────
typedef struct { const char *label; uint32_t min; uint32_t max; } filter_preset_t;

static const filter_preset_t k_filters[] = {
    { "Todos",  0x000,  0x1FFFFFFF },   // ALL IDs (standard 11-bit + extended 29-bit)
    { "Motor",  0x100,  0x2FF      },
    { "Painel", 0x300,  0x4FF      },
    { "BCM",    0x500,  0x6FF      },
    { "ABS",    0x1C0,  0x1FF      },
    { "OBD-II", 0x7DF,  0x7EF      },
};
#define NUM_FILTERS  (int)(sizeof(k_filters) / sizeof(k_filters[0]))
#define IDX_CUSTOM   NUM_FILTERS   // "Custom" button index (beyond presets)

// ─────────────────────────────────────────────────────
// Screen state
// ─────────────────────────────────────────────────────
static lv_obj_t   *s_scr         = NULL;
static lv_timer_t *s_timer       = NULL;
static lv_obj_t   *s_tbl         = NULL;
static lv_obj_t   *s_lbl_status  = NULL;
static lv_obj_t   *s_lbl_info    = NULL;
static lv_obj_t   *s_btn_toggle  = NULL;
static lv_obj_t   *s_filter_btns[NUM_FILTERS + 1];   // +1 for Custom
static bool        s_running     = false;
static int         s_active_filter = 0;   // persists across screen transitions
static uint32_t    s_prev_id_count = 0;

// Custom filter state (persists)
static uint32_t    s_custom_min = 0x000;
static uint32_t    s_custom_max = 0x1FFFFFFF;

// Hex keyboard dialog state
#define KBD_MAX_HEX  8
static char        s_kbd_vals[2][KBD_MAX_HEX + 1];
static int         s_kbd_active = 0;
static lv_obj_t   *s_kbd_field_lbl[2]  = {NULL, NULL};
static lv_obj_t   *s_kbd_field_cont[2] = {NULL, NULL};

// Snapshot buffer (static — not on stack)
static app_can_id_entry_t s_snapshot[APP_CAN_MAX_IDS];

// ─────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────

static void set_toggle_appearance(void)
{
    if (!s_btn_toggle) return;
    lv_obj_t *lbl = lv_obj_get_child(s_btn_toggle, 0);
    if (!lbl) return;
    lv_label_set_text(lbl, s_running ? LV_SYMBOL_STOP " STOP" : LV_SYMBOL_PLAY " START");
    lv_obj_center(lbl);
    lv_obj_set_style_bg_color(s_btn_toggle, s_running ? ZOTTI_RED : ZOTTI_GREEN, 0);
}

static void apply_filter(int idx)
{
    if (idx < 0 || idx > IDX_CUSTOM) return;
    s_active_filter = idx;

    uint32_t fmin, fmax;
    if (idx == IDX_CUSTOM) {
        fmin = s_custom_min;
        fmax = s_custom_max;
    } else {
        fmin = k_filters[idx].min;
        fmax = k_filters[idx].max;
    }
    app_can_set_filter(fmin, fmax);
    app_can_clear_id_table();
    s_prev_id_count = 0;

    for (int i = 0; i <= IDX_CUSTOM; i++) {
        if (s_filter_btns[i]) {
            lv_obj_set_style_bg_color(s_filter_btns[i],
                (i == idx) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_CARD, 0);
        }
    }
}

// ─────────────────────────────────────────────────────
// Table update (called from poll timer — inside LVGL task)
// ─────────────────────────────────────────────────────

static void update_table(void)
{
    if (!s_tbl) return;

    uint32_t count = app_can_get_id_table(s_snapshot, APP_CAN_MAX_IDS);
    if (count == 0) return;

    // Grow row count only if needed (never shrink during a session)
    if (count > s_prev_id_count) {
        lv_table_set_row_count(s_tbl, count);
        s_prev_id_count = count;
    }

    char cell[48];
    for (uint32_t r = 0; r < count; r++) {
        const app_can_id_entry_t *e = &s_snapshot[r];

        // Col 0: ID
        if (e->extd) {
            snprintf(cell, sizeof(cell), "%lX", (unsigned long)e->id);
        } else {
            snprintf(cell, sizeof(cell), "%03lX", (unsigned long)e->id);
        }
        lv_table_set_cell_value(s_tbl, r, 0, cell);

        // Col 1: Tp
        lv_table_set_cell_value(s_tbl, r, 1, e->extd ? "EXT" : "STD");

        // Col 2: DLC
        snprintf(cell, sizeof(cell), "%u", e->dlc);
        lv_table_set_cell_value(s_tbl, r, 2, cell);

        // Col 3: Data bytes
        {
            int pos = 0;
            for (int i = 0; i < e->dlc && i < 8; i++) {
                pos += snprintf(cell + pos, sizeof(cell) - (size_t)pos,
                                i ? " %02X" : "%02X", e->data[i]);
                if (pos >= (int)sizeof(cell) - 3) break;
            }
        }
        lv_table_set_cell_value(s_tbl, r, 3, cell);

        // Col 4: Count
        snprintf(cell, sizeof(cell), "%lu", (unsigned long)e->count);
        lv_table_set_cell_value(s_tbl, r, 4, cell);

        // Col 5: Hz
        if (e->hz_x10 == 0) {
            lv_table_set_cell_value(s_tbl, r, 5, "---");
        } else {
            snprintf(cell, sizeof(cell), "%lu.%u",
                     (unsigned long)(e->hz_x10 / 10),
                     (unsigned)(e->hz_x10 % 10));
            lv_table_set_cell_value(s_tbl, r, 5, cell);
        }
    }
}

static void update_status(void)
{
    if (!s_lbl_status || !s_lbl_info) return;
    app_can_status_t st;
    app_can_sniffer_get_status(&st);

    lv_obj_set_style_text_color(s_lbl_status,
        st.state == APP_CAN_STATE_RUNNING ? ZOTTI_GREEN :
        st.state == APP_CAN_STATE_ERROR   ? ZOTTI_RED   : ZOTTI_GRAY, 0);

    const char *state_str = (st.state == APP_CAN_STATE_RUNNING) ? "RUN" :
                            (st.state == APP_CAN_STATE_ERROR)   ? "ERR" : "STOP";
    const char *log_label = (s_active_filter == IDX_CUSTOM)
                            ? "Custom"
                            : k_filters[s_active_filter].label;

    lv_label_set_text_fmt(s_lbl_status,
        "%s  TWAI:%s  SD:%s  LOG:%s",
        state_str,
        st.driver_ready ? "OK" : "OFF",
        st.sd_mounted   ? "OK" : "OFF",
        st.log_open     ? log_label : "OFF");

    lv_label_set_text_fmt(s_lbl_info,
        "IDs:%lu  Frames:%lu  Erros:%lu  %s",
        (unsigned long)st.unique_ids,
        (unsigned long)st.frames,
        (unsigned long)st.rx_errors,
        st.log_open ? app_can_sniffer_log_path() : (st.last_error[0] ? st.last_error : "---"));
}

// ─────────────────────────────────────────────────────
// Timer callback (200 ms — inside LVGL task)
// ─────────────────────────────────────────────────────

static void poll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    update_table();
    update_status();
}

// ─────────────────────────────────────────────────────
// Custom filter hex keyboard dialog
// ─────────────────────────────────────────────────────

static void kbd_refresh_labels(void)
{
    for (int f = 0; f < 2; f++) {
        if (s_kbd_field_lbl[f]) {
            lv_label_set_text_fmt(s_kbd_field_lbl[f], "0x%s", s_kbd_vals[f]);
        }
        if (s_kbd_field_cont[f]) {
            lv_obj_set_style_border_color(s_kbd_field_cont[f],
                (f == s_kbd_active) ? ZOTTI_ACCENT : lv_color_hex(0x1A3A5C), 0);
        }
    }
}

static void kbd_select_field_cb(lv_event_t *e)
{
    s_kbd_active = (int)(intptr_t)lv_event_get_user_data(e);
    kbd_refresh_labels();
}

static void kbd_digit_cb(lv_event_t *e)
{
    char ch = (char)(intptr_t)lv_event_get_user_data(e);
    char *buf = s_kbd_vals[s_kbd_active];
    size_t len = strlen(buf);
    if (len == 1 && buf[0] == '0') {
        buf[0] = ch;    // replace leading zero
    } else if (len < KBD_MAX_HEX) {
        buf[len] = ch;
        buf[len + 1] = '\0';
    }
    kbd_refresh_labels();
}

static void kbd_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    char *buf = s_kbd_vals[s_kbd_active];
    size_t len = strlen(buf);
    if (len > 1) {
        buf[len - 1] = '\0';
    } else {
        buf[0] = '0';
        buf[1] = '\0';
    }
    kbd_refresh_labels();
}

static void kbd_clear_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_kbd_vals[s_kbd_active][0] = '0';
    s_kbd_vals[s_kbd_active][1] = '\0';
    kbd_refresh_labels();
}

static void kbd_confirm_cb(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);

    uint32_t new_min = (uint32_t)strtoul(s_kbd_vals[0], NULL, 16);
    uint32_t new_max = (uint32_t)strtoul(s_kbd_vals[1], NULL, 16);

    if (new_min > new_max) {
        uint32_t tmp = new_min; new_min = new_max; new_max = tmp;
    }
    s_custom_min = new_min;
    s_custom_max = new_max;

    s_kbd_field_lbl[0] = s_kbd_field_lbl[1] = NULL;
    s_kbd_field_cont[0] = s_kbd_field_cont[1] = NULL;
    if (overlay) lv_obj_delete(overlay);

    apply_filter(IDX_CUSTOM);
}

static void kbd_cancel_cb(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    s_kbd_field_lbl[0] = s_kbd_field_lbl[1] = NULL;
    s_kbd_field_cont[0] = s_kbd_field_cont[1] = NULL;
    if (overlay) lv_obj_delete(overlay);
}

static void open_custom_filter_dialog(void)
{
    if (!s_scr) return;

    // Populate buffers with current custom values
    snprintf(s_kbd_vals[0], sizeof(s_kbd_vals[0]), "%lX", (unsigned long)s_custom_min);
    snprintf(s_kbd_vals[1], sizeof(s_kbd_vals[1]), "%lX", (unsigned long)s_custom_max);
    s_kbd_active = 0;

    // Semi-transparent overlay
    lv_obj_t *overlay = lv_obj_create(s_scr);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog box: 500×270 centered
    lv_obj_t *dlg = lv_obj_create(overlay);
    lv_obj_set_size(dlg, 500, 270);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0D1F33), 0);
    lv_obj_set_style_border_color(dlg, ZOTTI_ACCENT, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_radius(dlg, 8, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(dlg, 0, 0);

    // Title
    lv_obj_t *lbl_title = lv_label_create(dlg);
    lv_label_set_text(lbl_title, LV_SYMBOL_EDIT "  FILTRO PERSONALIZADO");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);

    // Field containers: MIN at x=20 y=42, MAX at x=262 y=42
    static const char *field_names[] = {"ID MIN", "ID MAX"};
    for (int f = 0; f < 2; f++) {
        s_kbd_field_cont[f] = lv_obj_create(dlg);
        lv_obj_set_size(s_kbd_field_cont[f], 218, 36);
        lv_obj_set_pos(s_kbd_field_cont[f], 20 + f * 242, 42);
        lv_obj_set_style_bg_color(s_kbd_field_cont[f], lv_color_hex(0x061525), 0);
        lv_obj_set_style_radius(s_kbd_field_cont[f], 4, 0);
        lv_obj_set_style_border_width(s_kbd_field_cont[f], 2, 0);
        lv_obj_clear_flag(s_kbd_field_cont[f], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_kbd_field_cont[f], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_kbd_field_cont[f], kbd_select_field_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)f);

        lv_obj_t *lbl_name = lv_label_create(s_kbd_field_cont[f]);
        lv_label_set_text(lbl_name, field_names[f]);
        lv_obj_set_style_text_font(lbl_name, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0x6B8EAD), 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 8, 0);

        s_kbd_field_lbl[f] = lv_label_create(s_kbd_field_cont[f]);
        lv_obj_set_style_text_font(s_kbd_field_lbl[f], ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(s_kbd_field_lbl[f], ZOTTI_WHITE, 0);
        lv_obj_align(s_kbd_field_lbl[f], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    // Hex keyboard: 2 rows of 8 keys (0-7, 8-F)
    // Key size: 50×36, col spacing: 58px, row spacing: 44px
    // Row 1 y=90, Row 2 y=134
    static const char hex_keys[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };
    for (int ki = 0; ki < 16; ki++) {
        int row = ki / 8;
        int col = ki % 8;
        lv_obj_t *btn = lv_btn_create(dlg);
        lv_obj_set_size(btn, 50, 36);
        lv_obj_set_pos(btn, 20 + col * 58, 90 + row * 44);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0A2543), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, kbd_digit_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)hex_keys[ki]);
        lv_obj_t *lbl_k = lv_label_create(btn);
        char kstr[2] = {hex_keys[ki], 0};
        lv_label_set_text(lbl_k, kstr);
        lv_obj_set_style_text_font(lbl_k, ZOTTI_FONT_SMALL, 0);
        lv_obj_center(lbl_k);
    }

    // Del / Limpar — y=182
    lv_obj_t *btn_del = lv_btn_create(dlg);
    lv_obj_set_size(btn_del, 116, 32);
    lv_obj_set_pos(btn_del, 20, 182);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x3A1A00), 0);
    lv_obj_set_style_radius(btn_del, 4, 0);
    lv_obj_add_event_cb(btn_del, kbd_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_del = lv_label_create(btn_del);
    lv_label_set_text(lbl_del, LV_SYMBOL_LEFT " Del");
    lv_obj_set_style_text_font(lbl_del, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_del);

    lv_obj_t *btn_clr = lv_btn_create(dlg);
    lv_obj_set_size(btn_clr, 116, 32);
    lv_obj_set_pos(btn_clr, 144, 182);
    lv_obj_set_style_bg_color(btn_clr, lv_color_hex(0x3A0018), 0);
    lv_obj_set_style_radius(btn_clr, 4, 0);
    lv_obj_add_event_cb(btn_clr, kbd_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_clr = lv_label_create(btn_clr);
    lv_label_set_text(lbl_clr, LV_SYMBOL_TRASH " Limpar");
    lv_obj_set_style_text_font(lbl_clr, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_clr);

    // Action buttons
    lv_obj_t *btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, 160, 34);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_set_style_bg_color(btn_cancel, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_radius(btn_cancel, 4, 0);
    lv_obj_add_event_cb(btn_cancel, kbd_cancel_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, LV_SYMBOL_CLOSE "  Cancelar");
    lv_obj_set_style_text_font(lbl_cancel, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_ok = lv_btn_create(dlg);
    lv_obj_set_size(btn_ok, 160, 34);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_ok, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_ok, 4, 0);
    lv_obj_add_event_cb(btn_ok, kbd_confirm_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, LV_SYMBOL_OK "  Aplicar");
    lv_obj_set_style_text_font(lbl_ok, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_ok);

    kbd_refresh_labels();
}

// ─────────────────────────────────────────────────────
// Event callbacks
// ─────────────────────────────────────────────────────

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    // Sniffer keeps running in background (fulltime collecting — do NOT stop here)
    s_scr = s_tbl = s_lbl_status = s_lbl_info = s_btn_toggle = NULL;
    for (int i = 0; i <= IDX_CUSTOM; i++) s_filter_btns[i] = NULL;
    s_prev_id_count = 0;
    s_kbd_field_lbl[0] = s_kbd_field_lbl[1] = NULL;
    s_kbd_field_cont[0] = s_kbd_field_cont[1] = NULL;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_nav(ui_screen_can_show);
}

static void toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_running) {
        app_can_sniffer_stop();
        s_running = false;
    } else {
        esp_err_t err = app_can_sniffer_start();
        s_running = (err == ESP_OK);
    }
    set_toggle_appearance();
    update_status();
}

static void clear_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_can_clear_id_table();
    if (s_tbl) {
        lv_table_set_row_count(s_tbl, 0);
        s_prev_id_count = 0;
    }
}

static void open_sd_browser_from_sniffer(void) { ui_screen_sd_browser_show(ui_screen_can_sniffer_show); }

static void sd_browse_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_nav(open_sd_browser_from_sniffer);
}

static void filter_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == IDX_CUSTOM) {
        open_custom_filter_dialog();
    } else {
        apply_filter(idx);
    }
}

// ─────────────────────────────────────────────────────
// Screen creation
// ─────────────────────────────────────────────────────

void ui_screen_can_sniffer_show(void)
{
    s_prev_id_count = 0;
    // s_active_filter, s_custom_min, s_custom_max intentionally persist

    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    // ── Header (42px) ────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 800, 42);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 90, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " CAN");
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl);

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "CAN SNIFFER");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, -40, 0);

    lv_obj_t *btn_sd = lv_btn_create(hdr);
    lv_obj_set_size(btn_sd, 90, 28);
    lv_obj_align(btn_sd, LV_ALIGN_RIGHT_MID, -115, 0);
    lv_obj_set_style_bg_color(btn_sd, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_radius(btn_sd, 4, 0);
    lv_obj_add_event_cb(btn_sd, sd_browse_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_sd);
    lv_label_set_text(lbl, LV_SYMBOL_SAVE " SD");
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl);

    lv_obj_t *btn_clr = lv_btn_create(hdr);
    lv_obj_set_size(btn_clr, 74, 28);
    lv_obj_align(btn_clr, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_clr, lv_color_hex(0x3A2000), 0);
    lv_obj_set_style_radius(btn_clr, 4, 0);
    lv_obj_add_event_cb(btn_clr, clear_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_clr);
    lv_label_set_text(lbl, LV_SYMBOL_TRASH " IDs");
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl);

    s_btn_toggle = lv_btn_create(hdr);
    lv_obj_set_size(s_btn_toggle, 100, 28);
    lv_obj_align(s_btn_toggle, LV_ALIGN_RIGHT_MID, -202, 0);
    lv_obj_set_style_radius(s_btn_toggle, 4, 0);
    lv_obj_add_event_cb(s_btn_toggle, toggle_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_toggle);
    lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);

    // ── Status bar (32px) ─────────────────────────────
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 32);
    lv_obj_set_pos(status_bar, 0, 42);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_status, "STOP  TWAI:OFF  SD:OFF  LOG:OFF");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 10, 0);

    // ── Info bar (28px) ───────────────────────────────
    lv_obj_t *info_bar = lv_obj_create(scr);
    lv_obj_set_size(info_bar, 800, 28);
    lv_obj_set_pos(info_bar, 0, 74);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0x06111E), 0);
    lv_obj_set_style_border_width(info_bar, 0, 0);
    lv_obj_clear_flag(info_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_info = lv_label_create(info_bar);
    lv_label_set_text(s_lbl_info, "IDs:0  Frames:0  Erros:0  ---");
    lv_obj_set_style_text_font(s_lbl_info, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_info, lv_color_hex(0x6B8EAD), 0);
    lv_obj_align(s_lbl_info, LV_ALIGN_LEFT_MID, 10, 0);

    // ── Filter bar (34px) ─────────────────────────────
    // 7 buttons × 80px = 560, 6 gaps × 5px = 30, label ~50px → ~648px total (fits 800)
    lv_obj_t *filter_bar = lv_obj_create(scr);
    lv_obj_set_size(filter_bar, 800, 34);
    lv_obj_set_pos(filter_bar, 0, 102);
    lv_obj_set_style_bg_color(filter_bar, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(filter_bar, 0, 0);
    lv_obj_set_layout(filter_bar, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(filter_bar, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_pad_all(filter_bar, 4, 0);
    lv_obj_set_style_pad_column(filter_bar, 5, 0);
    lv_obj_set_style_flex_main_place(filter_bar, LV_FLEX_ALIGN_START, 0);
    lv_obj_clear_flag(filter_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_fil = lv_label_create(filter_bar);
    lv_label_set_text(lbl_fil, "Filtro:");
    lv_obj_set_style_text_font(lbl_fil, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_fil, ZOTTI_GRAY, 0);
    lv_obj_set_style_pad_top(lbl_fil, 5, 0);
    lv_obj_set_style_pad_left(lbl_fil, 4, 0);

    // Create preset + custom buttons (IDX_CUSTOM = NUM_FILTERS is the last slot)
    for (int i = 0; i <= IDX_CUSTOM; i++) {
        s_filter_btns[i] = lv_btn_create(filter_bar);
        lv_obj_set_size(s_filter_btns[i], 80, 24);
        lv_obj_set_style_bg_color(s_filter_btns[i],
            (i == s_active_filter) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_CARD, 0);
        lv_obj_set_style_radius(s_filter_btns[i], 3, 0);
        lv_obj_add_event_cb(s_filter_btns[i], filter_btn_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lbl = lv_label_create(s_filter_btns[i]);
        lv_label_set_text(lbl, (i < NUM_FILTERS) ? k_filters[i].label : "Custom");
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_center(lbl);
    }

    // ── Column header (24px) ──────────────────────────
    lv_obj_t *col_hdr = lv_obj_create(scr);
    lv_obj_set_size(col_hdr, 800, 24);
    lv_obj_set_pos(col_hdr, 0, 136);
    lv_obj_set_style_bg_color(col_hdr, lv_color_hex(0x0A2543), 0);
    lv_obj_set_style_border_width(col_hdr, 0, 0);
    lv_obj_clear_flag(col_hdr, LV_OBJ_FLAG_SCROLLABLE);

    static const struct { const char *name; int32_t x; } cols[] = {
        { "ID",    12  },
        { "Tp",    100 },
        { "DLC",   145 },
        { "Data",  185 },
        { "Count", 608 },
        { "Hz",    698 },
    };
    for (int i = 0; i < 6; i++) {
        lbl = lv_label_create(col_hdr);
        lv_label_set_text(lbl, cols[i].name);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_TINY, 0);
        lv_obj_set_style_text_color(lbl, ZOTTI_ACCENT, 0);
        lv_obj_set_pos(lbl, cols[i].x, 4);
    }

    // ── ID table (scrollable) ─────────────────────────
    lv_obj_t *tbl_cont = lv_obj_create(scr);
    lv_obj_set_size(tbl_cont, 800, 320);
    lv_obj_set_pos(tbl_cont, 0, 160);
    lv_obj_set_style_bg_color(tbl_cont, lv_color_hex(0x050B14), 0);
    lv_obj_set_style_pad_all(tbl_cont, 0, 0);
    lv_obj_set_style_border_width(tbl_cont, 0, 0);
    lv_obj_set_scroll_dir(tbl_cont, LV_DIR_VER);
    lv_obj_set_style_radius(tbl_cont, 0, 0);

    s_tbl = lv_table_create(tbl_cont);
    lv_obj_set_width(s_tbl, 790);
    lv_table_set_column_count(s_tbl, 6);
    lv_table_set_column_width(s_tbl, 0, 85);
    lv_table_set_column_width(s_tbl, 1, 42);
    lv_table_set_column_width(s_tbl, 2, 36);
    lv_table_set_column_width(s_tbl, 3, 415);
    lv_table_set_column_width(s_tbl, 4, 80);
    lv_table_set_column_width(s_tbl, 5, 70);

    lv_obj_set_style_bg_color(s_tbl, lv_color_hex(0x050B14), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_tbl, lv_color_hex(0x050B14), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_tbl, lv_color_hex(0xBDEEFF), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_tbl, ZOTTI_FONT_TINY, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_tbl, lv_color_hex(0x0A2543), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_tbl, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_tbl, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_tbl, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_tbl, 4, LV_PART_ITEMS);
    lv_obj_clear_flag(s_tbl, LV_OBJ_FLAG_CLICKABLE);

    lv_table_set_row_count(s_tbl, 0);

    // Sync running state with actual sniffer (may have been started at boot)
    app_can_status_t init_st;
    app_can_sniffer_get_status(&init_st);
    s_running = (init_st.state == APP_CAN_STATE_RUNNING);

    // Try to start if not already running
    if (!s_running) {
        esp_err_t err = app_can_sniffer_start();
        s_running = (err == ESP_OK);
    }

    // Re-apply current filter (does not reset ID table — just updates software filter)
    {
        uint32_t fmin, fmax;
        if (s_active_filter == IDX_CUSTOM) {
            fmin = s_custom_min;
            fmax = s_custom_max;
        } else {
            fmin = k_filters[s_active_filter].min;
            fmax = k_filters[s_active_filter].max;
        }
        app_can_set_filter(fmin, fmax);
    }

    set_toggle_appearance();
    update_status();

    // Poll timer 200ms
    s_timer = lv_timer_create(poll_timer_cb, 200, NULL);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "CAN sniffer screen ready (running=%d, filter=%d)", s_running, s_active_filter);
}
