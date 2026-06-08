#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

void ui_splash_create(void);
void ui_tabs_create(void);

void ui_screen_animacao_create(void *parent);
void ui_screen_entradas_create(void *parent);
void ui_screen_saidas_create(void *parent);
void ui_screen_calculos_create(void *parent);
void ui_screen_botoes_create(void *parent);

#ifdef __cplusplus
}
#endif

#endif