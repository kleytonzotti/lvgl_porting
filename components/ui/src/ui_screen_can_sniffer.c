#include "ui.h"
#include "app_can.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#define LOG_TEXT_CAP 8192

static const char *TAG = "UI_CAN_SNIFF";

static lv_timer_t *s_poll_timer = NULL;
static lv_obj_t *s_log_label = NULL;
static lv_obj_t *s_log_panel = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_count = NULL;
static lv_obj_t *s_btn_toggle = NULL;
static char s_log_text[LOG_TEXT_CAP];
static size_t s_log_len = 0;
static bool s_running = false;

static void append_log_line(const char *line)
{
    if (!line || !s_log_label) {
        return;
    }

    size_t line_len = strlen(line);
    size_t needed = line_len + 1;

    while (s_log_len + needed + 1 >= sizeof(s_log_text)) {
        char *first_nl = strchr(s_log_text, '\n');
        if (!first_nl) {
            s_log_len = 0;
            s_log_text[0] = '\0';
            break;
        }

        size_t drop = (size_t)(first_nl - s_log_text) + 1;
        memmove(s_log_text, s_log_text + drop, s_log_len - drop + 1);
        s_log_len -= drop;
    }

    memcpy(s_log_text + s_log_len, line, line_len);
    s_log_len += line_len;
    s_log_text[s_log_len++] = '\n';
    s_log_text[s_log_len] = '\0';

    lv_label_set_text(s_log_label, s_log_text);
    if (s_log_panel) {
        lv_obj_scroll_to_y(s_log_panel, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

static void update_status(void)
{
    app_can_status_t st;
    app_can_sniffer_get_status(&st);

    if (s_lbl_count) {
        lv_label_set_text_fmt(s_lbl_count, "Frames: %lu  Drop: %lu",
                              (unsigned long)st.frames,
                              (unsigned long)st.dropped_lines);
    }

    if (s_lbl_status) {
        const char *state = "STOP";
        if (st.state == APP_CAN_STATE_RUNNING) {
            state = "RUN";
        } else if (st.state == APP_CAN_STATE_ERROR) {
            state = "ERR";
        }

        lv_label_set_text_fmt(s_lbl_status, "%s | TWAI:%s | SD:%s | LOG:%s",
                              state,
                              st.driver_ready ? "OK" : "OFF",
                              st.sd_mounted ? "OK" : "OFF",
                              st.log_open ? "OK" : "OFF");

        lv_obj_set_style_text_color(s_lbl_status,
            st.state == APP_CAN_STATE_RUNNING ? ZOTTI_GREEN :
            st.state == APP_CAN_STATE_ERROR ? ZOTTI_RED : ZOTTI_GRAY, 0);
    }
}

static void set_toggle_label(void)
{
    if (!s_btn_toggle) {
        return;
    }

    lv_obj_t *lbl = lv_obj_get_child(s_btn_toggle, 0);
    if (!lbl) {
        return;
    }

    lv_label_set_text(lbl, s_running ? LV_SYMBOL_STOP " STOP" : LV_SYMBOL_PLAY " START");
    lv_obj_center(lbl);
    lv_obj_set_style_bg_color(s_btn_toggle, s_running ? ZOTTI_RED : ZOTTI_GREEN, 0);
}

static void poll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    char line[APP_CAN_SNIFF_LINE_MAX];
    for (int i = 0; i < 12; i++) {
        if (!app_can_sniffer_poll_line(line, sizeof(line))) {
            break;
        }
        append_log_line(line);
    }

    update_status();
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_poll_timer) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    if (s_running) {
        app_can_sniffer_stop();
        s_running = false;
    }
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
        append_log_line("SNIF STOP requested");
    } else {
        esp_err_t err = app_can_sniffer_start();
        s_running = (err == ESP_OK);
        append_log_line(s_running ? "SNIF START requested" : "SNIF START failed");
    }

    set_toggle_label();
    update_status();
}

void ui_screen_can_sniffer_show(void)
{
    s_log_len = 0;
    s_log_text[0] = '\0';
    s_running = false;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 42);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 90, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " CAN");
    lv_obj_set_style_text_font(lbl_back, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_back);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "CAN SNIFFER");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_btn_toggle = lv_btn_create(header);
    lv_obj_set_size(s_btn_toggle, 110, 28);
    lv_obj_align(s_btn_toggle, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_radius(s_btn_toggle, 4, 0);
    lv_obj_add_event_cb(s_btn_toggle, toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_toggle = lv_label_create(s_btn_toggle);
    lv_obj_set_style_text_font(lbl_toggle, ZOTTI_FONT_TINY, 0);

    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 800, 46);
    lv_obj_set_pos(status_bar, 0, 42);
    lv_obj_set_style_bg_color(status_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_status, "STOP | TWAI:OFF | SD:OFF | LOG:OFF");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 10, -8);

    s_lbl_count = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_count, "Frames: 0  Drop: 0");
    lv_obj_set_style_text_font(s_lbl_count, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_count, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_count, LV_ALIGN_LEFT_MID, 10, 12);

    lv_obj_t *lbl_path = lv_label_create(status_bar);
    lv_label_set_text_fmt(lbl_path, "Log: %s", app_can_sniffer_log_path());
    lv_obj_set_style_text_font(lbl_path, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_path, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_path, LV_ALIGN_RIGHT_MID, -10, 0);

    s_log_panel = lv_obj_create(scr);
    lv_obj_set_size(s_log_panel, 780, 370);
    lv_obj_set_pos(s_log_panel, 10, 100);
    lv_obj_set_style_bg_color(s_log_panel, lv_color_hex(0x050B14), 0);
    lv_obj_set_style_border_color(s_log_panel, ZOTTI_BORDER, 0);
    lv_obj_set_style_border_width(s_log_panel, 1, 0);
    lv_obj_set_style_radius(s_log_panel, 4, 0);
    lv_obj_set_style_pad_all(s_log_panel, 8, 0);
    lv_obj_set_scroll_dir(s_log_panel, LV_DIR_VER);

    s_log_label = lv_label_create(s_log_panel);
    lv_obj_set_width(s_log_label, 744);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_log_label, "");
    lv_obj_set_style_text_font(s_log_label, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_log_label, lv_color_hex(0xD6E8F5), 0);
    lv_obj_align(s_log_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_poll_timer = lv_timer_create(poll_timer_cb, 100, NULL);

    esp_err_t err = app_can_sniffer_start();
    s_running = (err == ESP_OK);
    set_toggle_label();
    append_log_line(s_running ? "SNIF START requested" : "SNIF START failed");
    update_status();

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "CAN sniffer screen shown");
}
