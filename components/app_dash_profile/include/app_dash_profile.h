#ifndef APP_DASH_PROFILE_H
#define APP_DASH_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Perfis salvos do dashboard (estilo de mostrador, RPM de corte, tema de
// cor) — persistidos na NVS (flash), sobrevivem a reinício. Cada perfil é
// só configuração de como MOSTRAR os dados; nunca guarda dado de sensor.
// ─────────────────────────────────────────────────────

#define APP_DASH_PROFILE_MAX       8
#define APP_DASH_PROFILE_NAME_LEN  24

typedef enum {
    APP_DASH_GAUGE_DIGITAL = 0,
    APP_DASH_GAUGE_ANALOG  = 1,
} app_dash_gauge_style_t;

// Modelo de tela — arranjo inteiro dos widgets, não só o estilo do
// mostrador de RPM. Cada um tem seu próprio builder em ui_screen_dashboard.c.
//   CLASSIC: o layout original (arco/digital + 3 colunas de sensores).
//   RACE:    estilo FuelTech — RPM digital grande + barra de shift light
//            progressiva + G-meter (usa accel_g do app_ecu/app_sim).
//   GRID:    estilo Injepro — numérico central + ponteiro analógico +
//            grade de mostradores menores com mínimo/máximo salvo.
typedef enum {
    APP_DASH_LAYOUT_CLASSIC = 0,
    APP_DASH_LAYOUT_RACE    = 1,
    APP_DASH_LAYOUT_GRID    = 2,
} app_dash_layout_t;

typedef struct {
    char                   name[APP_DASH_PROFILE_NAME_LEN];
    app_dash_gauge_style_t gauge_style;
    app_dash_layout_t      layout;
    uint16_t               redline_rpm;
    uint8_t                color_theme;   // índice na paleta definida na UI
} app_dash_profile_t;

// Carrega da NVS; se não existir nenhum perfil salvo ainda, cria um
// "Padrão" (digital, corte 7000rpm) e o marca como ativo.
void app_dash_profile_init(void);

uint32_t app_dash_profile_count(void);
bool     app_dash_profile_get(uint32_t index, app_dash_profile_t *out);

// index == app_dash_profile_count() cria um novo perfil (se houver espaço,
// até APP_DASH_PROFILE_MAX); qualquer outro index válido sobrescreve.
bool app_dash_profile_save(uint32_t index, const app_dash_profile_t *profile);
bool app_dash_profile_delete(uint32_t index);

// -1 = nenhum perfil ativo definido (usa o padrão embutido).
int32_t app_dash_profile_get_active_index(void);
void    app_dash_profile_set_active_index(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
