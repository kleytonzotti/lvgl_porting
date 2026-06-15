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

// ----- Sub-telas -----
void ui_screen_dashboard_show(void);
void ui_screen_scanner_show(void);
void ui_screen_ecu_show(void);
void ui_screen_can_show(void);
void ui_screen_can_sniffer_show(void);
void ui_screen_sd_browser_show(void);
void ui_screen_datalogger_show(void);
void ui_screen_config_show(void);
void ui_screen_sistema_show(void);

// ----- Legado (mantidas para nao quebrar build) -----
void ui_splash_create(void);
void ui_tabs_create(void);
void ui_screen_animacao_create(lv_obj_t *parent);
void ui_screen_entradas_create(lv_obj_t *parent);
void ui_screen_saidas_create(lv_obj_t *parent);
void ui_screen_calculos_create(lv_obj_t *parent);
void ui_screen_botoes_create(lv_obj_t *parent);
void ui_screen_touch_create(lv_obj_t *parent);
void ui_screen_touch_update(lv_coord_t x, lv_coord_t y, bool pressed);

#ifdef __cplusplus
}
#endif

#endif
