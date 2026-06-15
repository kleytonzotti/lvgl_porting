#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SNIFF";

// ─────────────────────────────────────────────────────
// Preset filters: {label, id_min, id_max}
// ─────────────────────────────────────────────────────
typedef struct { const char *label; uint32_t min; uint32_t max; } filter_preset_t;

static const filter_preset_t k_filters[] = {
    { "Todos",  0x000,  0x7FF  },
    { "Motor",  0x100,  0x2FF  },
    { "Painel", 0x300,  0x4FF  },
    { "BCM",    0x500,  0x6FF  },
    { "ABS",    0x1C0,  0x1FF  },
    { "OBD-II", 0x7DF,  0x7EF  },
};
#define NUM_FILTERS  (int)(sizeof(k_filters) / sizeof(k_filters[0]))

// ─────────────────────────────────────────────────────
// Screen state
// ─────────────────────────────────────────────────────
static lv_timer_t *s_timer       = NULL;
static lv_obj_t   *s_tbl         = NULL;
static lv_obj_t   *s_lbl_status  = NULL;
static lv_obj_t   *s_lbl_info    = NULL;
static lv_obj_t   *s_btn_toggle  = NULL;
static lv_obj_t   *s_filter_btns[NUM_FILTERS];
static bool        s_running     = false;
static int         s_active_filter = 0;    // index into k_filters[]
static uint32_t    s_prev_id_count = 0;

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
    if (idx < 0 || idx >= NUM_FILTERS) return;
    s_active_filter = idx;
    app_can_set_filter(k_filters[idx].min, k_filters[idx].max);
    app_can_clear_id_table();
    s_prev_id_count = 0;

    // Update button appearance
    for (int i = 0; i < NUM_FILTERS; i++) {
        if (s_filter_btns[i]) {
            lv_obj_set_style_bg_color(s_filter_btns[i],
                (i == idx) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_HEADER, 0);
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
    lv_label_set_text_fmt(s_lbl_status,
        "%s  TWAI:%s  SD:%s  LOG:%s",
        state_str,
        st.driver_ready ? "OK" : "OFF",
        st.sd_mounted   ? "OK" : "OFF",
        st.log_open     ? "OK" : "OFF");

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
// Event callbacks
// ─────────────────────────────────────────────────────

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_running) { app_can_sniffer_stop(); s_running = false; }
    s_tbl = s_lbl_status = s_lbl_info = s_btn_toggle = NULL;
    for (int i = 0; i < NUM_FILTERS; i++) s_filter_btns[i] = NULL;
    s_prev_id_count = 0;
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

static void sd_browse_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_nav(ui_screen_sd_browser_show);
}

static void filter_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    apply_filter(idx);
}

// ─────────────────────────────────────────────────────
// Screen creation
// ─────────────────────────────────────────────────────

void ui_screen_can_sniffer_show(void)
{
    s_running       = false;
    s_prev_id_count = 0;
    s_active_filter = 0;

    lv_obj_t *scr = lv_obj_create(NULL);
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

    // Back button
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

    // Title
    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "CAN SNIFFER");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, -40, 0);

    // SD browser button
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

    // Clear IDs button
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

    // START/STOP button
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

    for (int i = 0; i < NUM_FILTERS; i++) {
        s_filter_btns[i] = lv_btn_create(filter_bar);
        lv_obj_set_size(s_filter_btns[i], 95, 24);
        lv_obj_set_style_bg_color(s_filter_btns[i],
            (i == 0) ? ZOTTI_ACCENT_DIM : ZOTTI_BG_CARD, 0);
        lv_obj_set_style_radius(s_filter_btns[i], 3, 0);
        lv_obj_add_event_cb(s_filter_btns[i], filter_btn_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lbl = lv_label_create(s_filter_btns[i]);
        lv_label_set_text(lbl, k_filters[i].label);
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

    // Column widths must match the table below
    //      ID=85  Tp=42  DLC=36  Data=415  Count=80  Hz=70  (sum ~728, rest padding)
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

    // ── ID table (scrollable, remaining height) ───────
    // Y=160  Height = 480-160 = 320px
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
    lv_table_set_column_width(s_tbl, 0, 85);   // ID
    lv_table_set_column_width(s_tbl, 1, 42);   // Tp
    lv_table_set_column_width(s_tbl, 2, 36);   // DLC
    lv_table_set_column_width(s_tbl, 3, 415);  // Data
    lv_table_set_column_width(s_tbl, 4, 80);   // Count
    lv_table_set_column_width(s_tbl, 5, 70);   // Hz

    // Style: dark background, thin borders
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

    // Apply initial filter + start appearance
    app_can_set_filter(k_filters[0].min, k_filters[0].max);
    set_toggle_appearance();
    update_status();

    // Poll timer 200ms
    s_timer = lv_timer_create(poll_timer_cb, 200, NULL);

    // Auto-start sniffer
    esp_err_t err = app_can_sniffer_start();
    s_running = (err == ESP_OK);
    set_toggle_appearance();
    update_status();

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "CAN sniffer screen ready");
}
