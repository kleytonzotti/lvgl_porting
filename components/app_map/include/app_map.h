#ifndef APP_MAP_H
#define APP_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// MAPAS de tunagem (injeção, ignição e sonda) — grade RPM x carga (MAP em
// kPa), mesmo conceito de VE table/ignition table/AFR target table do
// Speeduino/MegaSquirt/rusEFI já citados no ROADMAP.md §7.
//
// "Sonda" aqui é o mapa de LAMBDA ALVO pra malha fechada (closed-loop): a
// ECU usa isso pra saber que mistura perseguir em cada ponto de RPM/carga
// (rico sob carga alta/boost, próximo de estequiométrico em marcha lenta
// e cruzeiro) e corrige a injeção comparando com a leitura real da sonda
// lambda — não confundir com o CAMPO `lambda`/`lambda_x1000` do app_ecu
// (ROADMAP.md §4), que é a leitura AO VIVO da sonda, não um alvo.
//
// Isto é dado de CALIBRAÇÃO do motor — bem diferente da telemetria
// somente-leitura do app_ecu (ROADMAP.md §4): aqui é o PAINEL que ESCREVE
// na ECU. É uma exceção deliberada à "regra de ouro" do ROADMAP.md §1
// (painel nunca manda comando pra ECU), decidida em sessão explícita — não
// um acidente de arquitetura (ver ROADMAP.md §13 pra decisão registrada).
//
// A ECU é quem tem a palavra final: o firmware dela (fora deste repo) DEVE
// validar/clampar os valores recebidos e RECUSAR gravar um mapa novo com o
// motor girando (RPM > 0) — só aceita com motor parado. Isso é
// responsabilidade do firmware da ECU; aqui só definimos o protocolo, o
// editor e o cache local.
//
// Eixos compartilhados entre os três mapas (mesma grade RPM x kPa). Valores
// guardados em ponto fixo — inteiro não depende de como cada lado
// (ESP32 / STM32) serializa float, então não tem risco de divergência de
// representação entre os dois firmwares:
//   Injeção: décimos de milissegundo (ex.: 42 = 4.2ms).
//   Ignição: décimos de grau, com sinal (ex.: 150 = 15.0° APMS, -20 = -2.0°).
//   Sonda:   centésimos de lambda (ex.: 85 = 0.85 — rico; 100 = 1.00 —
//            estequiométrico). Centésimos (não décimos) porque a faixa
//            útil de lambda é bem mais estreita — precisão de 0.1 seria
//            grosseira demais pra malha fechada.
// ─────────────────────────────────────────────────────

#define APP_MAP_RPM_BINS   8
#define APP_MAP_LOAD_BINS  6

#define APP_MAP_INJ_MIN_TENTHS    5      //  0.5 ms
#define APP_MAP_INJ_MAX_TENTHS    300    // 30.0 ms
#define APP_MAP_IGN_MIN_TENTHS   (-100)  // -10.0 graus (retardo)
#define APP_MAP_IGN_MAX_TENTHS    600    //  60.0 graus (avanço)
#define APP_MAP_SONDA_MIN_X100    60     // lambda 0.60 (bem rico, plena carga/boost)
#define APP_MAP_SONDA_MAX_X100    130    // lambda 1.30 (bem pobre, economia)

typedef enum {
    APP_MAP_TABLE_INJECAO = 0,
    APP_MAP_TABLE_IGNICAO = 1,
    APP_MAP_TABLE_SONDA   = 2,
    APP_MAP_TABLE_COUNT,
} app_map_table_id_t;

typedef struct {
    uint16_t rpm_bins[APP_MAP_RPM_BINS];        // eixo RPM, crescente
    uint16_t load_kpa_bins[APP_MAP_LOAD_BINS];  // eixo carga (MAP), crescente
    int16_t  injecao[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];  // decimos de ms
    int16_t  ignicao[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];  // decimos de grau
    int16_t  sonda[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];    // centesimos de lambda (alvo)
} app_map_set_t;

// --- Ciclo de vida / cache LOCAL (NVS do painel, ESP32) ---
// Isto é só o cache do painel pra reabrir a tela no último mapa editado —
// a cópia que manda de verdade é a gravada na FLASH da ECU (STM32), lida
// por ela sozinha a cada boot (ver o guia do firmware da ECU). Se o painel
// perder o NVS (reset de fábrica) mas a ECU não, a ECU continua com o mapa
// dela — os dois lados não dependem um do outro pra manter o próprio dado.
void app_map_init(void);
void app_map_get(app_map_set_t *out);
bool app_map_save_local(const app_map_set_t *in);

// Preenche 'out' com uma curva base "segura pra editar" — NÃO é calibração
// real de nenhum motor, é só um ponto de partida plausível pra não começar
// de uma tabela zerada. Sempre recalibrar no motor real antes de rodar.
void app_map_reset_default(app_map_set_t *out);

// ─────────────────────────────────────────────────────
// Protocolo BLE Painel -> ECU (característica de ESCRITA) — transferência
// em pedaços (chunks) pra não depender de negociação de MTU grande: cada
// pacote cabe em 20 bytes de payload ATT (MTU padrão BLE, ATT_MTU 23 - 3
// de overhead ATT). Se o MTU negociado for maior, funciona igual, só que
// cada pacote fica com folga sem usar.
//
//   [0] 0xEA           marcador (distingue de 0xEC, notificação de telemetria)
//   [1] versão (0x01)
//   [2] msg_type       ver app_map_msg_type_t
//   [3..N-2] payload especifico do tipo (ver cada bloco abaixo)
//   [N-1] checksum = XOR de todos os bytes anteriores DESTE PACOTE (não da
//                    tabela inteira — cada pacote se autovalida sozinho)
//
// MAP_BEGIN  (payload 6 bytes -> pacote de 9 bytes):
//   [3]    table_id (app_map_table_id_t: 0=injecao 1=ignicao 2=sonda)
//   [4]    rpm_bins_count
//   [5]    load_bins_count
//   [6..7] total_len (uint16 LE) — bytes que virão via MAP_CHUNK (eixos + celulas)
//   [8]    checksum
//
// MAP_CHUNK  (payload variavel, ate 16 bytes de dado -> pacote ate 20 bytes):
//   [3..4] seq (uint16 LE), começa em 0
//   [5]    len (bytes de dado neste pacote, <=16)
//   [6..6+len-1] dado (fatia do buffer serializado — ver app_map_serialize_table)
//   [6+len] checksum
//
// MAP_END  (payload 4 bytes -> pacote de 7 bytes):
//   [3]    table_id (deve bater com o do BEGIN)
//   [4..5] crc16 (CCITT-FALSE, poly 0x1021 init 0xFFFF) do buffer inteiro
//          reassemblado pelos MAP_CHUNK — a ECU recalcula e compara antes
//          de aceitar; se não bater, ela responde ERR_CRC e descarta tudo
//   [6]    checksum
//
// MAP_ABORT (sem payload alem do checksum, pacote de 4 bytes) — cancela
// uma transferência em andamento (ex.: usuário saiu da tela no meio do envio).
//
// Resposta ECU -> Painel (característica de NOTIFY dedicada, SEPARADA da
// notificação de telemetria do app_ecu):
//   [0] 0xEB           marcador de status de mapa
//   [1] versão (0x01)
//   [2] status         ver app_map_status_t
//   [3] table_id
//   [4] checksum
// ─────────────────────────────────────────────────────

#define APP_MAP_FRAME_MARKER_CMD    0xEA   // Painel -> ECU
#define APP_MAP_FRAME_MARKER_STATUS 0xEB   // ECU -> Painel
#define APP_MAP_PROTO_VERSION       0x01
#define APP_MAP_CHUNK_MAX_DATA      16     // bytes de dado por pacote MAP_CHUNK
#define APP_MAP_PACKET_MAX_LEN      (6 + APP_MAP_CHUNK_MAX_DATA + 1)  // maior pacote possivel (MAP_CHUNK cheio)

typedef enum {
    APP_MAP_MSG_BEGIN = 0x01,
    APP_MAP_MSG_CHUNK = 0x02,
    APP_MAP_MSG_END   = 0x03,
    APP_MAP_MSG_ABORT = 0x04,
} app_map_msg_type_t;

typedef enum {
    APP_MAP_STATUS_ACK_BEGIN          = 0x00,
    APP_MAP_STATUS_ACK_CHUNK          = 0x01,
    APP_MAP_STATUS_SAVED_OK           = 0x02,
    APP_MAP_STATUS_ERR_CRC            = 0x03,
    APP_MAP_STATUS_ERR_ENGINE_RUNNING = 0x04,
    APP_MAP_STATUS_ERR_OUT_OF_RANGE   = 0x05,
    APP_MAP_STATUS_ERR_BUSY           = 0x06,
} app_map_status_t;

// Tamanho do buffer serializado (eixos + a matriz de celulas de UMA tabela)
// que viaja fatiado dentro dos MAP_CHUNK.
#define APP_MAP_SERIALIZED_LEN \
    (APP_MAP_RPM_BINS * 2 + APP_MAP_LOAD_BINS * 2 + APP_MAP_LOAD_BINS * APP_MAP_RPM_BINS * 2)

// CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF) — mesma conta que a ECU
// precisa fazer pra validar o MAP_END. Exposta pra ser usada nos dois
// lados (painel e, futuramente, firmware da ECU) sem risco de divergir.
uint16_t app_map_crc16(const uint8_t *data, size_t len);

// Serializa os eixos + a matriz de células de UMA tabela (injecao, ignicao
// OU sonda, conforme table_id) no formato que viaja nos MAP_CHUNK. 'out'
// precisa ter pelo menos APP_MAP_SERIALIZED_LEN bytes.
void app_map_serialize_table(const app_map_set_t *set, app_map_table_id_t table_id,
                              uint8_t *out);

// Monta os pacotes MAP_BEGIN/MAP_CHUNK/MAP_END prontos pra escrever na
// characteristic BLE, chamando on_packet(ctx, pkt, pkt_len) um pacote por
// vez, na ordem. Isso só GERA os pacotes — não manda nada pelo ar; quem
// enviaria de verdade é app_map_send_to_ecu().
typedef void (*app_map_packet_cb_t)(void *ctx, const uint8_t *pkt, uint8_t pkt_len);
void app_map_build_packets(const app_map_set_t *set, app_map_table_id_t table_id,
                            app_map_packet_cb_t on_packet, void *ctx);

// Envia (de verdade, pelo ar) uma tabela pra ECU. Hoje sempre retorna
// ESP_ERR_NOT_SUPPORTED — falta o GATT client de escrita em app_ble.c,
// mesma pendência do subscribe de telemetria do app_ecu (ROADMAP.md §4).
// Quando isso existir, esta função passa a chamar app_map_build_packets()
// e escrever cada pacote na characteristic, aguardando o status via notify.
esp_err_t app_map_send_to_ecu(const app_map_set_t *set, app_map_table_id_t table_id);

#ifdef __cplusplus
}
#endif

#endif
