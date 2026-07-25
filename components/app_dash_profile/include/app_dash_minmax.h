#ifndef APP_DASH_MINMAX_H
#define APP_DASH_MINMAX_H

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Mínimo/máximo por canal, ao estilo do Injepro Dash Pro: cada mostrador
// guarda o menor e o maior valor vistos desde o último reset, e isso
// sobrevive a desligar (persistido na NVS). Só grava na NVS quando um
// mínimo/máximo novo é batido (raro depois do aquecimento) ou no reset
// explícito — nunca a cada leitura, pra não desgastar a flash.
// ─────────────────────────────────────────────────────

typedef struct {
    float rpm_min,   rpm_max;
    float speed_min, speed_max;
    float map_min,   map_max;
    float tps_min,   tps_max;
    float ect_min,   ect_max;
    float iat_min,   iat_max;
    float batt_min,  batt_max;
    float afr_min,   afr_max;
} app_dash_minmax_t;

// Carrega da NVS (ou inicializa min=máx=0 se nunca salvo antes).
void app_dash_minmax_init(void);

// Atualiza com uma nova leitura — só grava na NVS se algum recorde mudou.
void app_dash_minmax_update(float rpm, float speed, float map_kpa, float tps,
                             float ect, float iat, float batt, float afr);

void app_dash_minmax_get(app_dash_minmax_t *out);

// Zera todos os recordes pro valor atual (chamado pelo usuário na tela).
void app_dash_minmax_reset(void);

#ifdef __cplusplus
}
#endif

#endif
