#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// "Brilho" — na verdade um ESMAECIMENTO POR SOFTWARE, não backlight PWM de
// verdade. Este hardware (Waveshare ESP32-S3-Touch-LCD-4.3B) não tem isso:
// o backlight é uma linha digital LIGA/DESLIGA através do expansor I2C
// CH422G (`bsp_backlight_set()`, agrupado no header do BSP junto com
// SD/touch/CAN select — não é um pino PWM do ESP32). Não existe fiação
// física de dimming neste board; software não inventa um pino que não
// existe.
//
// O que ISTO faz de verdade: um retângulo preto translúcido no layer
// superior da LVGL (`lv_layer_top()`, por cima de qualquer tela, sem
// precisar mexer em cada tela individualmente), com opacidade inversamente
// proporcional ao "brilho" escolhido. Sobe o preto por cima do conteúdo —
// não desliga LED nenhum, não economiza energia do backlight (continua
// no mesmo consumo de sempre). É a mesma técnica usada por muitos
// dispositivos sem PWM de tela quando querem oferecer "diminuir o brilho"
// mesmo assim.
//
// NUNCA clicável (LV_OBJ_FLAG_CLICKABLE removida) — senão bloquearia todo
// toque da tela real por baixo. E nunca chega a preto total: o piso
// (ZOTTI_BRIGHTNESS_MIN) limita a opacidade máxima da superposição, pra
// não parecer que a tela travou/desligou.
// ─────────────────────────────────────────────────────

#define ZOTTI_BRIGHTNESS_MIN  20    // piso — mantem a tela legivel mesmo no minimo
#define ZOTTI_BRIGHTNESS_MAX  100

// Cria a superposição de esmaecimento (precisa da LVGL/display já
// inicializados — chamar de dentro de ui_init(), depois do BSP) e aplica
// o último valor salvo na NVS (ou um padrão reduzido — ver .c — na
// primeira vez que roda, já que o padrão de fábrica do backlight é claro
// demais pra uso noturno).
void zotti_brightness_init(void);

// Aplica e persiste o novo brilho (%), clampado em [ZOTTI_BRIGHTNESS_MIN,
// ZOTTI_BRIGHTNESS_MAX]. Efeito imediato em qualquer tela já aberta (o
// layer superior é global) — não precisa recriar nada.
void zotti_brightness_set(uint8_t pct);
uint8_t zotti_brightness_get(void);

#ifdef __cplusplus
}
#endif
