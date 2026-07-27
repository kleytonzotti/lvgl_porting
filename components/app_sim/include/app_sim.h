#ifndef APP_SIM_H
#define APP_SIM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Modo demonstração / simulação — gera valores plausíveis de um ciclo de
// condução (RPM, velocidade, aceleração, sondas do motor) sem precisar de
// nenhum hardware real conectado (ECU, CAN, sensores). Fica **totalmente
// separado** de app_ecu/app_can de propósito: é só uma fonte de dados falsa
// que a UI pode escolher usar no lugar da real, nunca o contrário — nada
// aqui nunca deve poder disfarçar como se fosse dado real gravado em log.
//
// Sobe sua própria task, roda o tempo todo (mesmo com o modo desligado, só
// não atualiza os dados), e é lido pela UI do mesmo jeito que app_ecu: um
// snapshot protegido por mutex.
// ─────────────────────────────────────────────────────

typedef struct {
    uint16_t rpm;
    uint8_t  speed_kph;
    uint8_t  throttle_pct;   // posição simulada do pedal (0-100)
    uint8_t  map_kpa;
    uint8_t  tps_pct;
    int8_t   ect_c;
    int8_t   iat_c;
    float    batt_v;
    float    lambda;         // 1.000 = estequiométrico
    float    accel_g;        // aceleração longitudinal estimada
} app_sim_data_t;

void app_sim_init(void);

void app_sim_set_enabled(bool enable);
bool app_sim_is_enabled(void);

// RPM a partir do qual o efeito de "perto do corte" deve disparar na UI
// (o app_sim usa o mesmo valor pra decidir quando simular rajadas perto do
// limite, então a demonstração e o efeito visual ficam sincronizados).
void     app_sim_set_redline(uint16_t redline_rpm);
uint16_t app_sim_get_redline(void);

void app_sim_get_data(app_sim_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
