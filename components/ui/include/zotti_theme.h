#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Paleta de cores da UI — trocável em tempo de execução (ver zotti_theme.c).
// Os nomes ZOTTI_* continuam os mesmos usados em toda a UI; só passaram a
// ler uma variável em vez de uma constante, então nenhuma tela precisou
// mudar pra ganhar suporte a tema. A troca só aparece em telas criadas
// DEPOIS de trocar o tema (não redesenha o que já está na tela).
// ─────────────────────────────────────────────────────

typedef enum {
    ZOTTI_THEME_1 = 0,   // "TEMA1" — o visual original (azul/ciano escuro)
    ZOTTI_THEME_2 = 1,   // "TEMA2" — esportivo (preto/vermelho, estilo FuelTech)
    ZOTTI_THEME_3 = 2,   // "TEMA3" — clássico (âmbar/creme, estilo painel analógico antigo)
    ZOTTI_THEME_COUNT
} zotti_theme_id_t;

extern lv_color_t g_zotti_bg;
extern lv_color_t g_zotti_bg_card;
extern lv_color_t g_zotti_bg_header;
extern lv_color_t g_zotti_accent;
extern lv_color_t g_zotti_accent_dim;
extern lv_color_t g_zotti_white;
extern lv_color_t g_zotti_gray;
extern lv_color_t g_zotti_gray_dark;
extern lv_color_t g_zotti_green;
extern lv_color_t g_zotti_yellow;
extern lv_color_t g_zotti_red;
extern lv_color_t g_zotti_border;

// Background
#define ZOTTI_BG            (g_zotti_bg)
#define ZOTTI_BG_CARD       (g_zotti_bg_card)
#define ZOTTI_BG_HEADER     (g_zotti_bg_header)

// Accent
#define ZOTTI_ACCENT        (g_zotti_accent)
#define ZOTTI_ACCENT_DIM    (g_zotti_accent_dim)

// Text
#define ZOTTI_WHITE         (g_zotti_white)
#define ZOTTI_GRAY          (g_zotti_gray)
#define ZOTTI_GRAY_DARK     (g_zotti_gray_dark)

// Status
#define ZOTTI_GREEN         (g_zotti_green)
#define ZOTTI_YELLOW        (g_zotti_yellow)
#define ZOTTI_RED           (g_zotti_red)

// Border
#define ZOTTI_BORDER        (g_zotti_border)

// Carrega o tema salvo na NVS (ou TEMA1 se nunca escolhido) e aplica.
void zotti_theme_init(void);

// Aplica e persiste o tema escolhido — passa a valer nas próximas telas
// criadas (não redesenha telas já abertas).
void zotti_theme_set(zotti_theme_id_t id);

zotti_theme_id_t zotti_theme_get(void);
const char      *zotti_theme_name(zotti_theme_id_t id);

#ifdef __cplusplus
}
#endif
