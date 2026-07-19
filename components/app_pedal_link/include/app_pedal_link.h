#ifndef APP_PEDAL_LINK_H
#define APP_PEDAL_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Protocolo tela <-> módulo de pedal, via UART (RS485 onboard,
// BSP_RS485_TX/RX). A tela é quem manda o modo e o heartbeat; o módulo de
// pedal SEMPRE decide sozinho entrar em passthrough se o heartbeat parar —
// essa lógica de failsafe mora no firmware do módulo de pedal, não aqui.
//
//   Comando  (tela -> pedal): [0xAA, modo, checksum]
//   Heartbeat(tela -> pedal): [0x55]                      a cada 200ms
//   Telemetria(pedal -> tela): [0xBB, pedal_pct, saida_pct, fault_flags, checksum]
//
//   checksum = XOR de todos os bytes anteriores do frame.
// ─────────────────────────────────────────────────────
#define APP_PEDAL_CMD_BYTE        0xAA
#define APP_PEDAL_HEARTBEAT_BYTE  0x55
#define APP_PEDAL_TELEMETRY_BYTE  0xBB
#define APP_PEDAL_HEARTBEAT_MS    200

typedef enum {
    APP_PEDAL_MODE_ECONOMIA = 0,
    APP_PEDAL_MODE_NORMAL   = 1,
    APP_PEDAL_MODE_SPORT    = 2,
} app_pedal_mode_t;

typedef struct {
    bool     link_ok;         // recebeu telemetria válida recentemente
    uint32_t last_update_ms;
    uint8_t  pedal_pct;        // posição real do pedal (0-100)
    uint8_t  output_pct;       // sinal que está sendo enviado à ECU (0-100)
    uint8_t  fault_flags;      // bitfield definido pelo firmware do módulo de pedal
    uint32_t frames_ok;
    uint32_t frames_bad_checksum;
} app_pedal_status_t;

// Inicializa a UART e sobe a task que envia heartbeat/comandos e lê telemetria.
esp_err_t app_pedal_link_init(void);

// Muda o modo ativo (manda o comando imediatamente; o heartbeat periódico
// continua reforçando o último modo enviado).
void app_pedal_link_set_mode(app_pedal_mode_t mode);

// Snapshot protegido por mutex — seguro chamar de qualquer task.
void app_pedal_link_get_status(app_pedal_status_t *out);

// Alimenta o parser de telemetria com bytes crus — é o que a task de leitura
// da UART chama internamente, exposto aqui pra separar o parser do
// transporte (também é o que os testes automatizados usam pra exercitar o
// parser sem precisar de UART/hardware de verdade).
void app_pedal_link_feed_bytes(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
