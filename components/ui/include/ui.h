#ifndef UI_H
#define UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----- Init -----
void ui_init(void);

// ----- Navegacao principal -----
void ui_splash_show(void);
void ui_menu_show(void);

// ----- Navegacao adiada (evita corrupcao mid-frame) -----
void ui_nav(void (*fn)(void));
void ui_screen_load(lv_obj_t *scr);

// ----- Sub-telas -----
void ui_screen_dashboard_show(void);

// Paleta de cor do acento do RPM (ROADMAP.md §12) — usada pelo dashboard
// pra pintar o arco/ponteiro e pela tela de Config pra montar o dropdown
// "Cor". idx e ciclico (qualquer valor funciona, "% UI_DASH_ACCENT_COUNT").
#define UI_DASH_ACCENT_COUNT 6
lv_color_t  ui_dash_accent_color(uint8_t idx);
const char *ui_dash_accent_name(uint8_t idx);

void ui_screen_scanner_show(void);
void ui_screen_ecu_show(void);
void ui_screen_can_show(void);
void ui_screen_can_sniffer_show(void);
void ui_screen_sd_browser_show(void (*back_fn)(void));
void ui_screen_datalogger_show(void);
void ui_screen_config_show(void);
void ui_screen_sistema_show(void);
void ui_screen_pedal_show(void);

// ----- Usadas por ui_screen_sistema.c -----
void ui_screen_touch_create(lv_obj_t *parent);
void ui_screen_touch_update(lv_coord_t x, lv_coord_t y, bool pressed);

#ifdef __cplusplus
}
#endif

#endif
