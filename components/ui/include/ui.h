#ifndef UI_H
#define UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

void ui_splash_create(void);
void ui_tabs_create(void);

void ui_screen_animacao_create(lv_obj_t *parent);
void ui_screen_entradas_create(lv_obj_t *parent);
void ui_screen_saidas_create(lv_obj_t *parent);
void ui_screen_calculos_create(lv_obj_t *parent);
void ui_screen_botoes_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif