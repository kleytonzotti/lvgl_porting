#ifndef APP_ECU_H
#define APP_ECU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// Protocolo de telemetria ECU -> Tela (BLE notify), v1.
//
// Sentido único: a ECU programável apenas NOTIFICA dados: não existe
// characteristic de escrita usada por este lado. A tela não manda nenhum
// comando de controle para a ECU — só lê. Isso é proposital (requisito de
// segurança: a tela nunca deve poder alterar o funcionamento da ECU).
//
// Frame (little-endian):
//   [0] 0xEC              start marker
//   [1] versao (0x01)
//   [2] tamanho do payload (N)
//   [3..3+N-1] payload (ver app_ecu_payload_v1_t)
//   [3+N] checksum = XOR de todos os bytes anteriores (bytes 0..3+N-1)
//
// Payload v1 (14 bytes) cabe num único notify BLE mesmo com MTU padrao
// (ATT_MTU 23 -> 20 bytes uteis; frame completo = 3+14+1 = 18 bytes).
// ─────────────────────────────────────────────────────
#define APP_ECU_FRAME_START     0xEC
#define APP_ECU_PROTO_VERSION   0x01
#define APP_ECU_PAYLOAD_V1_LEN  14
#define APP_ECU_FRAME_MAX_LEN   (3 + APP_ECU_PAYLOAD_V1_LEN + 1)

typedef enum {
    APP_ECU_STATE_DISCONNECTED = 0,
    APP_ECU_STATE_CONNECTING,
    APP_ECU_STATE_CONNECTED,
} app_ecu_link_state_t;

// Valores já convertidos para unidade de engenharia (o parser faz a conta
// uma única vez, aqui — a tela só formata e mostra).
typedef struct {
    bool     valid;          // true assim que o primeiro frame v1 foi decodificado
    uint32_t last_update_ms; // xTaskGetTickCount() da última atualização (p/ detectar timeout)

    uint16_t rpm;
    uint8_t  map_kpa;
    uint8_t  tps_pct;
    int8_t   ect_c;
    int8_t   iat_c;
    float    batt_v;
    float    lambda;         // 1.000 = estequiométrico
    uint32_t ecu_uptime_ms;
} app_ecu_data_t;

typedef struct {
    app_ecu_link_state_t state;
    uint32_t frames_ok;
    uint32_t frames_bad_checksum;
    uint32_t frames_bad_len;
} app_ecu_status_t;

// --- Ciclo de vida ---
void app_ecu_init(void);

// --- Entrada de dados: quem receber os bytes brutos do notify BLE chama
// isso. Hoje nada chama (ver ROADMAP.md — falta o GATT client subscribe),
// mas a função em si é completa e testável com qualquer buffer de bytes. ---
void app_ecu_feed_ble_notify(const uint8_t *data, size_t len);

// --- Leitura (snapshot protegido por mutex — seguro chamar de qualquer task,
// inclusive da task do LVGL dentro de um lv_timer) ---
void app_ecu_get_data(app_ecu_data_t *out);
void app_ecu_get_status(app_ecu_status_t *out);

// Marca o estado de conexão (quem gerencia a conexão BLE — hoje ninguém,
// futuramente o GATT client — chama isso ao conectar/desconectar).
void app_ecu_set_link_state(app_ecu_link_state_t state);

#ifdef __cplusplus
}
#endif

#endif
