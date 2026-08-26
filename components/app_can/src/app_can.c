#include "app_can.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp_waveshare_43.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/twai.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define APP_CAN_RX_QUEUE_LEN    128
#define APP_CAN_TASK_STACK      4096
#define APP_CAN_TASK_PRIORITY   6
#define APP_CAN_SD_MOUNT        "/sdcard"
#define APP_CAN_FLUSH_EVERY     32   // flush CSV to SD every N frames

// 8KB: o buffer local de listagem (48 entradas ~2.3KB) + a profundidade de
// chamada do FATFS/VFS/SDSPI não cabem com folga numa stack de 4KB.
#define APP_CAN_SD_TASK_STACK      8192
#define APP_CAN_SD_TASK_PRIORITY   4
#define APP_CAN_SD_REQ_QUEUE_LEN   4

static const char *TAG = "APP_CAN";

// --- Per-ID table (protected by s_lock) ---
static app_can_id_entry_t s_id_table[APP_CAN_MAX_IDS];
static uint32_t           s_id_count = 0;

// --- State ---
static SemaphoreHandle_t s_lock           = NULL;
static TaskHandle_t      s_task           = NULL;
static FILE             *s_log_file       = NULL;
static sdmmc_card_t     *s_sd_card        = NULL;
static bool              s_spi_bus_ready  = false;
static bool              s_sd_mounted     = false;
static bool              s_driver_ready   = false;
static bool              s_driver_started = false;
static volatile bool     s_running        = false;
static uint32_t          s_flush_counter  = 0;
static char              s_log_path[40]   = "";
static app_can_status_t  s_status         = {0};

// --- Software filter ---
static volatile uint32_t s_filter_min = 0;
static volatile uint32_t s_filter_max = 0x1FFFFFFF;

// --- OBD2 ativo (ver app_can_obd2_set_active no header) ---
// O round robin de PIDs e a decodificação das respostas moraram na tela do
// CAN até agora; passaram pra cá pra que o dado sirva a QUALQUER consumidor
// (o dashboard lê a mesma fonte) e continue vivo com a tela fechada.
#define APP_CAN_OBD2_REQ_ID       0x7DFu   // broadcast Mode 01 (SAE J1979)
#define APP_CAN_OBD2_RESP_ID      0x7E8u   // primeira ECU a responder
#define APP_CAN_OBD2_REQ_PERIOD_MS 150     // um PID por vez, pra não inundar o barramento

static const uint8_t k_obd2_pids[] = {
    0x0C,  // RPM
    0x0D,  // Velocidade
    0x05,  // Temp. arrefecimento
    0x0F,  // Temp. ar admissão
    0x0B,  // MAP
    0x11,  // TPS
    0x42,  // Tensão da bateria
};
#define APP_CAN_OBD2_PID_COUNT  (int)(sizeof(k_obd2_pids) / sizeof(k_obd2_pids[0]))

static volatile bool s_obd2_active = false;
// Pausa explícita da task de captura. Não dá pra reaproveitar s_running pra
// isso: com o OBD2 ativo a task roda mesmo com o sniffer parado, então
// zerar s_running não a pararia mais (ver o gate em can_sniffer_task).
static volatile bool s_rx_paused   = false;
static app_can_obd2_data_t s_obd2_data = {0};   // protegido por s_lock

// --- SD assíncrono (task dedicada — ver app_can_sd_async_* no header) ---
typedef enum {
    SD_ASYNC_OP_MOUNT,
    SD_ASYNC_OP_LIST_DIR,
    SD_ASYNC_OP_DELETE,
    SD_ASYNC_OP_FORMAT,
} sd_async_op_t;

typedef struct {
    sd_async_op_t        op;
    char                  path[128];
    app_can_sd_done_cb_t  on_done;
} sd_async_req_t;

static QueueHandle_t      s_sd_req_queue   = NULL;
static TaskHandle_t       s_sd_task        = NULL;
static app_can_sd_entry_t s_async_dir_result[APP_CAN_SD_MAX_ENTRIES];
static int                s_async_dir_count = -1;
static esp_err_t          s_async_last_err  = ESP_OK;

// ─────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────

static app_can_id_entry_t *find_or_add_id_locked(uint32_t id, bool extd)
{
    for (uint32_t i = 0; i < s_id_count; i++) {
        if (s_id_table[i].id == id && s_id_table[i].extd == extd) {
            return &s_id_table[i];
        }
    }
    if (s_id_count >= APP_CAN_MAX_IDS) {
        return NULL;
    }
    app_can_id_entry_t *e = &s_id_table[s_id_count++];
    memset(e, 0, sizeof(*e));
    e->id   = id;
    e->extd = extd;
    s_status.unique_ids = s_id_count;
    return e;
}

// CSV: ms,type,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7
static void format_csv_line(const twai_message_t *msg, uint32_t ms,
                            char *out, size_t sz)
{
    int pos = snprintf(out, sz, "%lu,%s,%lX,%u",
                       (unsigned long)ms,
                       msg->extd ? "EXT" : "STD",
                       (unsigned long)msg->identifier,
                       (unsigned)msg->data_length_code);

    for (int i = 0; i < 8; i++) {
        if (pos >= (int)sz - 2) break;
        if (i < msg->data_length_code && i < TWAI_FRAME_MAX_DLC) {
            pos += snprintf(out + pos, sz - (size_t)pos, ",%02X", msg->data[i]);
        } else {
            out[pos++] = ',';
        }
    }
}

// Internal mount — format_if_failed=true is used only when user explicitly requests format.
static esp_err_t do_sd_mount(bool format_if_failed)
{
    if (s_sd_mounted) return ESP_OK;

    ESP_RETURN_ON_ERROR(bsp_sdcard_select(true), TAG, "SD select failed");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000; // reduz erros CRC (0x109) no slot TF desta placa

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = BSP_SD_MOSI,
        .miso_io_num     = BSP_SD_MISO,
        .sclk_io_num     = BSP_SD_SCLK,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    if (!s_spi_bus_ready) {
        esp_err_t e = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
        s_spi_bus_ready = true;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = format_if_failed,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(APP_CAN_SD_MOUNT, &host,
                                             &slot_cfg, &mount_cfg, &s_sd_card);
    if (err == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD mounted (format_if_failed=%d)", format_if_failed);
    }
    return err;
}

esp_err_t app_can_sd_mount(void)
{
    return do_sd_mount(false);
}

static void find_new_log_path(char *out, size_t sz)
{
    for (int n = 1; n <= 999; n++) {
        snprintf(out, sz, APP_CAN_SD_MOUNT "/can_%03d.csv", n);
        FILE *f = fopen(out, "r");
        if (!f) return;   // file does not exist → use this slot
        fclose(f);
    }
    snprintf(out, sz, APP_CAN_SD_MOUNT "/can_999.csv");
}

static esp_err_t open_log_file_locked(void)
{
    if (s_log_file) return ESP_OK;

    find_new_log_path(s_log_path, sizeof(s_log_path));
    s_log_file = fopen(s_log_path, "w");
    if (!s_log_file) {
        s_log_path[0] = '\0';
        return ESP_FAIL;
    }

    // CSV header
    fputs("ms,type,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7\n", s_log_file);
    fflush(s_log_file);
    s_flush_counter = 0;
    snprintf(s_status.log_path, sizeof(s_status.log_path), "%s", s_log_path);
    s_status.log_open = true;
    return ESP_OK;
}

static void close_log_file_locked(void)
{
    if (s_log_file) {
        fflush(s_log_file);
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_status.log_open = false;
}

// Write one CSV line — protected by s_lock so close can't race.
static void write_csv_locked(const char *line)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_log_file) {
        fputs(line, s_log_file);
        fputc('\n', s_log_file);
        if (++s_flush_counter >= APP_CAN_FLUSH_EVERY) {
            fflush(s_log_file);
            s_flush_counter = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

static esp_err_t ensure_twai_driver(void)
{
    if (s_driver_started) return ESP_OK;

    ESP_RETURN_ON_ERROR(bsp_can_set_selected(true), TAG, "CAN select failed");

    if (!s_driver_ready) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
            BSP_CAN_TX, BSP_CAN_RX, TWAI_MODE_LISTEN_ONLY);
        g.rx_queue_len   = APP_CAN_RX_QUEUE_LEN;
        g.tx_queue_len   = 0;
        g.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                           TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;

        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t e = twai_driver_install(&g, &t, &f);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
        s_driver_ready = true;
    }

    esp_err_t e = twai_start();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_driver_started = true;
    return ESP_OK;
}

// ─────────────────────────────────────────────────────
// OBD2: decodificação das respostas Mode 01 (ROADMAP.md §6). O frame vem
// como [len][0x41][pid][A][B]... — as fórmulas por PID são as da tabela do
// roadmap. Chamada de dentro da task de captura, com s_lock tomado.
// ─────────────────────────────────────────────────────

static void obd2_decode_frame_locked(const twai_message_t *msg, uint32_t now_ms)
{
    if (msg->identifier != APP_CAN_OBD2_RESP_ID) return;
    if (msg->data_length_code < 5 || msg->data[1] != 0x41) return;

    uint8_t pid = msg->data[2];
    float A = (float)msg->data[3];
    float B = (float)msg->data[4];

    switch (pid) {
    case 0x0C: s_obd2_data.rpm       = (int32_t)((A * 256.0f + B) / 4.0f);   break;
    case 0x0D: s_obd2_data.speed_kph = (int32_t)A;                            break;
    case 0x05: s_obd2_data.ect_c     = (int32_t)(A - 40.0f);                  break;
    case 0x0F: s_obd2_data.iat_c     = (int32_t)(A - 40.0f);                  break;
    case 0x0B: s_obd2_data.map_kpa   = (int32_t)A;                            break;
    case 0x11: s_obd2_data.tps_pct   = (int32_t)(A * 100.0f / 255.0f);        break;
    case 0x42: s_obd2_data.batt_v    = (A * 256.0f + B) / 1000.0f;            break;
    default:   return;   // PID que não pedimos — não conta como "vivo"
    }

    s_obd2_data.last_rx_ms = now_ms;
    s_obd2_data.valid      = true;

    // DEBUG: Desativado por padrão — para ativar, descomente:
    // ESP_LOGI(TAG, "[OBD2-RX] PID=0x%02X A=%.0f B=%.0f | rpm=%ld spd=%ld ect=%ld iat=%ld map=%ld tps=%ld batt=%.2f",
    //          pid, A, B, s_obd2_data.rpm, s_obd2_data.speed_kph, s_obd2_data.ect_c,
    //          s_obd2_data.iat_c, s_obd2_data.map_kpa, s_obd2_data.tps_pct, s_obd2_data.batt_v);
}

// Um pedido por período, em round robin, e invalidação do snapshot quando o
// carro para de responder. Chamada a cada volta do laço da task — inclusive
// nas voltas em que o twai_receive só deu timeout, senão o round robin
// pararia num barramento silencioso.
static void obd2_poll_step(uint32_t now_ms)
{
    static uint32_t last_req_ms = 0;
    static int      req_idx     = 0;

    if (!s_obd2_active) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_obd2_data.valid && (now_ms - s_obd2_data.last_rx_ms) > APP_CAN_OBD2_STALE_MS) {
        // DEBUG: Desativado — timeout de 2s sem resposta OBD2, marcando inválido
        // ESP_LOGW(TAG, "[OBD2-STALE] Sem resposta por %ldms, marcando dados como inválidos",
        //          (long)(now_ms - s_obd2_data.last_rx_ms));
        s_obd2_data.valid = false;
    }
    xSemaphoreGive(s_lock);

    if ((now_ms - last_req_ms) < APP_CAN_OBD2_REQ_PERIOD_MS) return;
    last_req_ms = now_ms;

    uint8_t pid = k_obd2_pids[req_idx];
    // DEBUG: Desativado — requisição OBD2 a cada 150ms
    // ESP_LOGI(TAG, "[OBD2-TX] Requisição %d/%d: PID=0x%02X", req_idx + 1, APP_CAN_OBD2_PID_COUNT, pid);
    app_can_obd2_request_pid(pid);
    req_idx = (req_idx + 1) % APP_CAN_OBD2_PID_COUNT;
}

// ─────────────────────────────────────────────────────
// CAN capture task — core 0, priority 6
// ─────────────────────────────────────────────────────

static void can_sniffer_task(void *arg)
{
    (void)arg;
    char csv[APP_CAN_CSV_LINE_MAX];

    for (;;) {
        // Recebe enquanto o sniffer estiver rodando OU o OBD2 ativo — o
        // dashboard pode consumir OBD2 sem ninguém ter aberto a tela do
        // sniffer. s_rx_paused tem prioridade: é o que segura a task
        // enquanto o driver TWAI está sendo reinstalado.
        if (s_rx_paused || (!s_running && !s_obd2_active)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        obd2_poll_step((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));

        twai_message_t msg = {0};
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.rx_errors++;
            xSemaphoreGive(s_lock);
            continue;
        }

        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // OBD2 antes do filtro de software: o filtro é do sniffer (quais IDs
        // o usuário quer LOGAR) e não pode calar a resposta do 0x7E8, que é
        // o que alimenta o dashboard no modo CAN.
        if (s_obd2_active) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            obd2_decode_frame_locked(&msg, now_ms);
            xSemaphoreGive(s_lock);
        }

        // Software filter
        if (msg.identifier < s_filter_min || msg.identifier > s_filter_max) {
            continue;
        }

        // Tabela por ID e estatísticas são do sniffer — com só o OBD2 ligado
        // (tela do sniffer fechada) não faz sentido inflar os contadores.
        if (!s_running) continue;

        // Update per-ID table and frame counter
        xSemaphoreTake(s_lock, portMAX_DELAY);
        app_can_id_entry_t *e = find_or_add_id_locked(msg.identifier, msg.extd);
        if (e) {
            if (e->count > 0 && e->last_ms > 0) {
                uint32_t interval = now_ms - e->last_ms;
                if (interval >= 1 && interval < 60000) {
                    uint32_t hz10 = 10000u / interval;
                    e->hz_x10 = (hz10 > 0xFFFF) ? 0xFFFF : (uint16_t)hz10;
                }
            }
            e->last_ms = now_ms;
            e->dlc = msg.data_length_code;
            memcpy(e->data, msg.data, msg.data_length_code);
            e->count++;
        }
        s_status.frames++;
        xSemaphoreGive(s_lock);

        // Write CSV (mutex protects against close() race)
        format_csv_line(&msg, now_ms, csv, sizeof(csv));
        write_csv_locked(csv);
    }
}

// ─────────────────────────────────────────────────────
// SD worker task — única task que faz I/O de cartão SD. Todo o trabalho
// bloqueante (montar, listar diretório, apagar, formatar) acontece aqui,
// nunca na task do LVGL. Processa um pedido por vez, na ordem em que
// chegaram na fila.
// ─────────────────────────────────────────────────────

static void sd_worker_task(void *arg)
{
    (void)arg;
    sd_async_req_t req;

    for (;;) {
        if (xQueueReceive(s_sd_req_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (req.op) {
        case SD_ASYNC_OP_MOUNT: {
            esp_err_t err = app_can_sd_mount();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_async_last_err = err;
            xSemaphoreGive(s_lock);
            break;
        }
        case SD_ASYNC_OP_LIST_DIR: {
            // Escreve direto no buffer compartilhado — nada de cópia via
            // array local na pilha desta task (o antigo "tmp[48]" somava
            // ~2.3KB de pilha em cima da profundidade de chamada do
            // FATFS/VFS/SDSPI; era um risco real de estouro de pilha).
            // Seguro sem lock durante o próprio list_dir: só quem lê esse
            // buffer é app_can_sd_async_get_dir_result(), e só é lido
            // depois que req.on_done() dispara logo abaixo — nunca antes
            // desta chamada terminar.
            int n = app_can_sd_list_dir(req.path, s_async_dir_result, APP_CAN_SD_MAX_ENTRIES);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_async_dir_count = n;
            xSemaphoreGive(s_lock);
            break;
        }
        case SD_ASYNC_OP_DELETE: {
            bool ok = app_can_sd_delete_file(req.path);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_async_last_err = ok ? ESP_OK : ESP_FAIL;
            xSemaphoreGive(s_lock);
            break;
        }
        case SD_ASYNC_OP_FORMAT: {
            esp_err_t err = app_can_sd_format();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_async_last_err = err;
            xSemaphoreGive(s_lock);
            break;
        }
        }

        // Diagnóstico — mesma ideia do [DIAG-LVGL-TASK] em bsp_lvgl_port.c:
        // se algum dia essa task chegar perto de estourar a pilha, isso
        // aparece aqui em vez de a gente só ver sintomas em outro lugar.
        ESP_LOGI(TAG, "[DIAG-SD-TASK] op=%d stack_free=%uB",
                 (int)req.op, (unsigned)(uxTaskGetStackHighWaterMark(NULL) * 4u));

        if (req.on_done) {
            req.on_done();
        }
    }
}

static bool sd_async_submit(sd_async_op_t op, const char *path, app_can_sd_done_cb_t on_done)
{
    if (!s_sd_req_queue) return false;

    sd_async_req_t req = { .op = op, .on_done = on_done };
    if (path) {
        snprintf(req.path, sizeof(req.path), "%s", path);
    } else {
        req.path[0] = '\0';
    }

    // Timeout 0: quem chama (tipicamente a task do LVGL) nunca pode ficar
    // esperando espaço na fila — se estiver cheia, o pedido é descartado.
    return xQueueSend(s_sd_req_queue, &req, 0) == pdTRUE;
}

// ─────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────

esp_err_t app_can_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state       = APP_CAN_STATE_STOPPED;
    s_status.driver_ready = false;
    s_status.sd_mounted  = false;
    s_status.log_open    = false;
    s_status.last_error[0] = '\0';
    xSemaphoreGive(s_lock);

    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(can_sniffer_task, "can_sniff",
                                                APP_CAN_TASK_STACK, NULL,
                                                APP_CAN_TASK_PRIORITY, &s_task, 0);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }

    if (!s_sd_req_queue) {
        s_sd_req_queue = xQueueCreate(APP_CAN_SD_REQ_QUEUE_LEN, sizeof(sd_async_req_t));
        if (!s_sd_req_queue) return ESP_ERR_NO_MEM;
    }
    if (!s_sd_task) {
        BaseType_t ok = xTaskCreate(sd_worker_task, "sd_worker",
                                    APP_CAN_SD_TASK_STACK, NULL,
                                    APP_CAN_SD_TASK_PRIORITY, &s_sd_task);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "CAN service ready");
    return ESP_OK;
}

esp_err_t app_can_sniffer_start(void)
{
    if (s_running && s_status.log_open) return ESP_OK;  // fully running with log open

    // Running but no log (SD was missing at boot): retry SD mount + log open
    if (s_running) {
        bool sd_ok = (app_can_sd_mount() == ESP_OK);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.sd_mounted = sd_ok;
        if (sd_ok) {
            bool log_ok = (open_log_file_locked() == ESP_OK);
            if (!log_ok) {
                snprintf(s_status.last_error, sizeof(s_status.last_error), "Log open failed");
            } else {
                s_status.last_error[0] = '\0';
            }
        } else {
            snprintf(s_status.last_error, sizeof(s_status.last_error), "SD mount failed");
        }
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    if (!s_lock) {
        ESP_RETURN_ON_ERROR(app_can_init(), TAG, "CAN init failed");
    }

    esp_err_t err = ensure_twai_driver();
    if (err != ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.state = APP_CAN_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error), "TWAI start failed");
        xSemaphoreGive(s_lock);
        return err;
    }

    bool sd_ok  = (app_can_sd_mount() == ESP_OK);
    bool log_ok = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.sd_mounted   = sd_ok;
    s_status.driver_ready = s_driver_started;
    if (sd_ok) {
        log_ok = (open_log_file_locked() == ESP_OK);
    }
    if (!sd_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "SD mount failed");
    } else if (!log_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "Log open failed");
    } else {
        s_status.last_error[0] = '\0';
    }
    s_status.state  = APP_CAN_STATE_RUNNING;
    s_status.frames = 0;
    s_running = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sniffer started — log=%s", s_log_path[0] ? s_log_path : "none");
    return ESP_OK;
}

void app_can_sniffer_stop(void)
{
    s_running = false;
    // Wait for the CAN task to finish any in-progress write (receive timeout = 50ms)
    vTaskDelay(pdMS_TO_TICKS(100));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_log_file_locked();
    s_status.state = APP_CAN_STATE_STOPPED;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sniffer stopped — %lu frames", (unsigned long)s_status.frames);
}

uint32_t app_can_get_id_table(app_can_id_entry_t *out, uint32_t max_entries)
{
    if (!out || !s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t count = (s_id_count < max_entries) ? s_id_count : max_entries;
    memcpy(out, s_id_table, count * sizeof(app_can_id_entry_t));
    xSemaphoreGive(s_lock);
    return count;
}

void app_can_clear_id_table(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_id_table, 0, sizeof(s_id_table));
    s_id_count = 0;
    s_status.unique_ids = 0;
    s_status.frames     = 0;
    xSemaphoreGive(s_lock);
}

void app_can_set_filter(uint32_t id_min, uint32_t id_max)
{
    s_filter_min = id_min;
    s_filter_max = id_max;
}

void app_can_get_filter(uint32_t *id_min_out, uint32_t *id_max_out)
{
    if (id_min_out) *id_min_out = s_filter_min;
    if (id_max_out) *id_max_out = s_filter_max;
}

void app_can_sniffer_get_status(app_can_status_t *out)
{
    if (!out) return;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        *out = s_status;
        xSemaphoreGive(s_lock);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

const char *app_can_sniffer_log_path(void)
{
    return s_log_path[0] ? s_log_path : "---";
}

bool app_can_sd_is_mounted(void)
{
    return s_sd_mounted;
}

int app_can_sd_list_csv(app_can_sd_file_t *out, uint32_t max_files)
{
    if (!s_sd_mounted || !out) return -1;

    DIR *dir = opendir(APP_CAN_SD_MOUNT);
    if (!dir) return -1;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && (uint32_t)count < max_files) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        // match .csv extension (case-insensitive)
        const char *ext = name + len - 4;
        if (!(ext[0]=='.' && (ext[1]=='c'||ext[1]=='C') &&
              (ext[2]=='s'||ext[2]=='S') && (ext[3]=='v'||ext[3]=='V'))) continue;

        snprintf(out[count].name, sizeof(out[count].name), "%.*s",
                 (int)(sizeof(out[count].name) - 1), name);

        // Get size — usa o nome já truncado para que o compilador
        // possa verificar que full (64) cabe "/sdcard/" (8) + name (39) + \0
        char full[64];
        snprintf(full, sizeof(full), APP_CAN_SD_MOUNT "/%s", out[count].name);
        struct stat st;
        out[count].size_kb = (stat(full, &st) == 0) ? (uint32_t)(st.st_size / 1024) : 0;
        count++;
    }
    closedir(dir);
    return count;
}

int app_can_sd_list_dir(const char *path, app_can_sd_entry_t *out, uint32_t max_entries)
{
    if (!s_sd_mounted || !out || !path) return -1;

    DIR *dir = opendir(path);
    if (!dir) return -1;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && (uint32_t)count < max_entries) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;   // skip . and ..

        snprintf(out[count].name, sizeof(out[count].name), "%.*s",
                 (int)(sizeof(out[count].name) - 1), name);

        char full[172];   // 128 (path) + 1 (/) + 40 (name) + 3 pad
        snprintf(full, sizeof(full), "%s/%s", path, out[count].name);
        struct stat st;
        if (stat(full, &st) == 0) {
            out[count].is_dir  = S_ISDIR(st.st_mode);
            out[count].size_kb = out[count].is_dir ? 0 : (uint32_t)(st.st_size / 1024);
        } else {
            out[count].is_dir  = (ent->d_type == DT_DIR);
            out[count].size_kb = 0;
        }
        count++;
    }
    closedir(dir);
    return count;
}

bool app_can_sd_delete_file(const char *path)
{
    if (!path) return false;
    return (remove(path) == 0);
}

esp_err_t app_can_sd_format(void)
{
    if (!s_sd_mounted) {
        // Card not mounted: attempt mount with format_if_mount_failed=true.
        // This handles exFAT, NTFS, or corrupted FAT — ESP-IDF reformats to FAT32.
        return do_sd_mount(true);
    }

    // Already mounted: format explicitly.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_log_file_locked();
    xSemaphoreGive(s_lock);

    esp_err_t err = esp_vfs_fat_sdcard_format(APP_CAN_SD_MOUNT, s_sd_card);
    ESP_LOGI(TAG, "SD format: %s", esp_err_to_name(err));
    return err;
}

// --- SD assíncrono: só enfileira, a sd_worker_task faz o trabalho de verdade ---

bool app_can_sd_async_mount(app_can_sd_done_cb_t on_done)
{
    return sd_async_submit(SD_ASYNC_OP_MOUNT, NULL, on_done);
}

bool app_can_sd_async_list_dir(const char *path, app_can_sd_done_cb_t on_done)
{
    return sd_async_submit(SD_ASYNC_OP_LIST_DIR, path, on_done);
}

bool app_can_sd_async_delete_file(const char *path, app_can_sd_done_cb_t on_done)
{
    return sd_async_submit(SD_ASYNC_OP_DELETE, path, on_done);
}

bool app_can_sd_async_format(app_can_sd_done_cb_t on_done)
{
    return sd_async_submit(SD_ASYNC_OP_FORMAT, NULL, on_done);
}

int app_can_sd_async_get_dir_result(app_can_sd_entry_t *out, uint32_t max_entries)
{
    if (!out || !s_lock) return -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_async_dir_count;
    uint32_t copy_n = (n > 0) ? ((uint32_t)n < max_entries ? (uint32_t)n : max_entries) : 0;
    if (copy_n > 0) {
        memcpy(out, s_async_dir_result, copy_n * sizeof(out[0]));
    }
    xSemaphoreGive(s_lock);
    // Nunca devolver uma contagem maior do que o que foi de fato copiado —
    // um chamador que iterar "out[0..n-1]" sem isso pode ler além do próprio
    // buffer se algum dia passar um max_entries menor que 48.
    return (n > 0) ? (int)copy_n : n;
}

esp_err_t app_can_sd_async_get_last_err(void)
{
    if (!s_lock) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t e = s_async_last_err;
    xSemaphoreGive(s_lock);
    return e;
}

// ─────────────────────────────────────────────────────
// OBD2 ativo — reinstala o driver TWAI em modo NORMAL (transmite) ou volta
// pro LISTEN_ONLY padrão (só escuta). O modo só pode ser escolhido na
// instalação do driver, por isso precisa parar + desinstalar + reinstalar
// em vez de só trocar uma flag.
// ─────────────────────────────────────────────────────

esp_err_t app_can_obd2_set_active(bool enable)
{
    if (!s_lock) {
        ESP_RETURN_ON_ERROR(app_can_init(), TAG, "CAN init failed");
    }
    if (enable == s_obd2_active && s_driver_started) return ESP_OK;

    // Pausa a task de captura antes de mexer no driver — twai_receive() não
    // pode estar em andamento durante um uninstall/install. O timeout do
    // twai_receive é 50ms; 100ms garante que a task já voltou pro gate no
    // topo do laço antes de seguirmos. Usa s_rx_paused em vez de zerar
    // s_running: com o OBD2 ativo a task roda mesmo com o sniffer parado,
    // então mexer em s_running não a pararia (e ainda apagaria o estado do
    // sniffer por baixo de quem estivesse logando).
    s_rx_paused = true;
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(bsp_can_set_selected(true), TAG, "CAN select failed");

    if (s_driver_started) { twai_stop();             s_driver_started = false; }
    if (s_driver_ready)   { twai_driver_uninstall();  s_driver_ready   = false; }

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        BSP_CAN_TX, BSP_CAN_RX, enable ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len   = APP_CAN_RX_QUEUE_LEN;
    g.tx_queue_len   = enable ? 4 : 0;
    g.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                       TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t e = twai_driver_install(&g, &t, &f);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { s_rx_paused = false; return e; }
    s_driver_ready = true;

    e = twai_start();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { s_rx_paused = false; return e; }
    s_driver_started = true;
    s_obd2_active     = enable;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.driver_ready = true;
    // Zera o snapshot nas duas direções: ao ligar pra não mostrar valor de
    // uma sessão antiga antes da primeira resposta chegar; ao desligar
    // porque a fonte deixou de existir.
    memset(&s_obd2_data, 0, sizeof(s_obd2_data));
    xSemaphoreGive(s_lock);

    s_rx_paused = false;
    // DEBUG: Desativado — OBD2 ativado/desativado, troca de modo TWAI
    // ESP_LOGI(TAG, "[OBD2] Ativação=%d | Modo TWAI=%s | Driver=%d | Sniffer=OFF durante OBD2",
    //          enable, enable ? "NORMAL (TRANSMITE PIDs)" : "LISTEN_ONLY (SO ESCUTA)", s_driver_started);
    return ESP_OK;
}

bool app_can_obd2_is_active(void)
{
    return s_obd2_active;
}

void app_can_obd2_get_data(app_can_obd2_data_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_obd2_data;
    xSemaphoreGive(s_lock);

    // A invalidação por silêncio também precisa valer pra quem lê com o
    // OBD2 desligado ou a task parada (aí obd2_poll_step não roda).
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (out->valid && (now_ms - out->last_rx_ms) > APP_CAN_OBD2_STALE_MS) {
        // DEBUG: Desativado — dados ficaram stale (silence > 2s)
        // ESP_LOGW(TAG, "[OBD2-GET] Dados ficaram stale durante leitura (silence=%ldms)",
        //          (long)(now_ms - out->last_rx_ms));
        out->valid = false;
    }

    // DEBUG: Desativado — leitura periódica de snapshot OBD2 (dashboard/decoder)
    // static uint32_t last_debug_ms = 0;
    // if (out->valid && (now_ms - last_debug_ms) >= 1000) {
    //     last_debug_ms = now_ms;
    //     ESP_LOGI(TAG, "[OBD2-GET] rpm=%ld spd=%ld ect=%ld iat=%ld map=%ld tps=%ld batt=%.2f",
    //              out->rpm, out->speed_kph, out->ect_c, out->iat_c, out->map_kpa, out->tps_pct, out->batt_v);
    // }
}

esp_err_t app_can_obd2_request_pid(uint8_t pid)
{
    if (!s_obd2_active || !s_driver_started) {
        // DEBUG: Desativado — tentativa de TX com OBD2 inativo
        // ESP_LOGW(TAG, "[OBD2-TX] Ignorado: ativo=%d started=%d", s_obd2_active, s_driver_started);
        return ESP_ERR_INVALID_STATE;
    }

    twai_message_t msg = {0};
    msg.identifier       = 0x7DF;   // ID de broadcast padrao Mode 01 (SAE J1979)
    msg.data_length_code = 8;
    msg.data[0] = 0x02;   // 2 bytes uteis a seguir (modo + pid)
    msg.data[1] = 0x01;   // Mode 01 = dado atual
    msg.data[2] = pid;
    for (int i = 3; i < 8; i++) msg.data[i] = 0x55;   // padding padrao ISO 15765

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
    // DEBUG: Desativado — erro ao transmitir frame OBD2
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "[OBD2-TX] Falha ao transmitir PID 0x%02X: err=%d", pid, err);
    // }
    return err;
}
