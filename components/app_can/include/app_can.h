#ifndef APP_CAN_H
#define APP_CAN_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CAN_MAX_IDS        128   // max unique IDs tracked
#define APP_CAN_CSV_LINE_MAX    96   // max bytes per CSV line

typedef enum {
    APP_CAN_STATE_STOPPED = 0,
    APP_CAN_STATE_RUNNING,
    APP_CAN_STATE_ERROR,
} app_can_state_t;

// Latest data + statistics for one unique CAN ID.
typedef struct {
    uint32_t id;
    bool     extd;
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t count;      // total frames received for this ID
    uint32_t last_ms;    // tick ms of last frame (for Hz)
    uint16_t hz_x10;     // frequency * 10  (e.g. 105 = 10.5 Hz)
} app_can_id_entry_t;

// SD file entry for browser (CSV-only list — kept for backward compat).
typedef struct {
    char     name[40];   // filename only (e.g. "can_001.csv")
    uint32_t size_kb;
} app_can_sd_file_t;

// Generic directory entry for full SD browser.
typedef struct {
    char     name[40];
    uint32_t size_kb;
    bool     is_dir;
} app_can_sd_entry_t;

#define APP_CAN_SD_MAX_ENTRIES  48   // shared with the async dir-listing buffer

typedef struct {
    app_can_state_t state;
    bool     driver_ready;
    bool     sd_mounted;
    bool     log_open;
    uint32_t frames;
    uint32_t rx_errors;
    uint32_t unique_ids;
    char     last_error[64];
    char     log_path[40];   // current session file (e.g. "/sdcard/can_001.csv")
} app_can_status_t;

// --- Core lifecycle ---
esp_err_t  app_can_init(void);
esp_err_t  app_can_sniffer_start(void);
void       app_can_sniffer_stop(void);

// --- Per-ID monitor (snapshot copy — safe to call from any task) ---
uint32_t app_can_get_id_table(app_can_id_entry_t *out, uint32_t max_entries);
void     app_can_clear_id_table(void);

// --- Software filter: only record frames where id_min <= id <= id_max ---
// Default: 0, 0x1FFFFFFF (accept all).
void app_can_set_filter(uint32_t id_min, uint32_t id_max);
void app_can_get_filter(uint32_t *id_min_out, uint32_t *id_max_out);

// --- Status ---
void        app_can_sniffer_get_status(app_can_status_t *out);
const char *app_can_sniffer_log_path(void);

// --- SD card (mount independently of the sniffer) ---
esp_err_t app_can_sd_mount(void);
bool      app_can_sd_is_mounted(void);
int       app_can_sd_list_csv(app_can_sd_file_t *out, uint32_t max_files);
int       app_can_sd_list_dir(const char *path, app_can_sd_entry_t *out, uint32_t max_entries);
bool      app_can_sd_delete_file(const char *path);
esp_err_t app_can_sd_format(void);

// --- SD assíncrono: as funções acima fazem I/O bloqueante — chamar direto
// de dentro da task do LVGL trava a tela (montagem/listagem de cartão pode
// levar de dezenas de ms a segundos). As funções abaixo só enfileiram o
// pedido pra uma task dedicada e retornam na hora; "on_done" é chamado a
// partir dessa task dedicada (NÃO da task do LVGL) quando terminar — quem
// implementa on_done e precisa mexer em lv_obj deve usar lv_async_call lá
// dentro. Retornam false se a fila estiver cheia (pedido não enfileirado).
typedef void (*app_can_sd_done_cb_t)(void);

bool app_can_sd_async_mount(app_can_sd_done_cb_t on_done);
bool app_can_sd_async_list_dir(const char *path, app_can_sd_done_cb_t on_done);
bool app_can_sd_async_delete_file(const char *path, app_can_sd_done_cb_t on_done);
bool app_can_sd_async_format(app_can_sd_done_cb_t on_done);

// Resultado do último app_can_sd_async_list_dir concluído (chamar depois que
// on_done disparar). Mesmo retorno de app_can_sd_list_dir (-1 = erro).
int app_can_sd_async_get_dir_result(app_can_sd_entry_t *out, uint32_t max_entries);

// Resultado da última operação simples concluída (mount/delete/format).
esp_err_t app_can_sd_async_get_last_err(void);

// --- OBD2 ativo (opcional, ver ROADMAP.md §6) ---
// Por padrão o driver TWAI roda em modo LISTEN_ONLY: nunca transmite no
// barramento, seguro mesmo ligado num carro de verdade (é assim que o
// sniffer passivo funciona). Ativar o modo OBD2 reinstala o driver em modo
// NORMAL pra poder mandar requisições SAE J1979 Mode 01 (ID 0x7DF) e ler
// as respostas (0x7E8+) — ou seja, o painel passa a "falar" no barramento,
// não só escutar. Desligar volta pro LISTEN_ONLY. O sniffer continua
// funcionando normalmente nos dois modos (a diferença é só a capacidade
// de transmitir).
//
// Enquanto ativo, o PRÓPRIO componente pede os PIDs da tabela do ROADMAP.md
// §6 em round robin e decodifica as respostas na task de captura — não
// depende de nenhuma tela estar aberta. Quem quiser os valores lê o
// snapshot com app_can_obd2_get_data(). Ativar também liga a recepção
// mesmo sem o sniffer/log rodando (app_can_sniffer_start não é necessário).
esp_err_t app_can_obd2_set_active(bool enable);
bool      app_can_obd2_is_active(void);

// Snapshot decodificado dos PIDs Mode 01 (ROADMAP.md §6). "valid" só é
// verdadeiro enquanto chegou pelo menos uma resposta nos últimos
// APP_CAN_OBD2_STALE_MS — se o carro parar de responder (ignição
// desligada, cabo solto), volta pra false sozinho em vez de congelar o
// último valor na tela.
#define APP_CAN_OBD2_STALE_MS  2000

typedef struct {
    bool     valid;
    uint32_t last_rx_ms;   // tick ms da última resposta decodificada
    int32_t  rpm;
    int32_t  speed_kph;
    int32_t  map_kpa;
    int32_t  tps_pct;
    int32_t  ect_c;
    int32_t  iat_c;
    float    batt_v;
} app_can_obd2_data_t;

// Cópia segura de chamar de qualquer task (inclusive a do LVGL).
void app_can_obd2_get_data(app_can_obd2_data_t *out);

// Manda uma requisição Mode 01 pro PID indicado (frame padrão de 8 bytes,
// preenchido com 0x55 conforme convenção ISO 15765). Retorna
// ESP_ERR_INVALID_STATE se o modo OBD2 não estiver ativo. Uso avulso: o
// round robin dos PIDs padrão já roda sozinho enquanto o modo está ativo.
esp_err_t app_can_obd2_request_pid(uint8_t pid);

#ifdef __cplusplus
}
#endif

#endif
