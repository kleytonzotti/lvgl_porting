#include "app_ble.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "app_map_debug_ble.h"  // DEBUG TEMPORARIO — ver app_map_debug_ble.h pra como remover
#include "app_map.h"
#include "app_ecu.h"

static const char *TAG = "APP_BLE";
static volatile bool s_ready = false;
static uint8_t s_own_addr_type;

static SemaphoreHandle_t s_lock;
static app_ble_scan_result_t s_scan_results[APP_BLE_MAX_SCAN_RESULTS];
static uint32_t s_scan_count;
static app_ble_status_t s_status;

static void set_status(app_ble_state_t state, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    strncpy(s_status.status_text, text, sizeof(s_status.status_text) - 1);
    s_status.status_text[sizeof(s_status.status_text) - 1] = '\0';
    xSemaphoreGive(s_lock);
}

// ─────────────────────────────────────────────────────
// GATT CLIENT — fala com a ECU (STM32) conectada por app_ble_connect(),
// usando o mesmo serviço/characteristics documentados em
// STM32_ECU_SIMULADOR_BLE.md §4 (UUIDs canônicos em app_map.h):
//   Telemetria (Notify)   -> repassa bytes crus pra app_ecu_feed_ble_notify()
//   UDS Request (Write)   -> transporte de escrita pro protocolo de mapas
//   UDS Response (Notify) -> transporte de resposta pro protocolo de mapas
//
// Depois de CONECTAR (BLE_GAP_EVENT_CONNECT), descobre o serviço, depois
// as 3 characteristics, depois os CCCDs das duas de Notify e ativa
// notificação em cada um — só then app_map_send_to_ecu()/
// app_map_read_from_ecu() conseguem transacionar de verdade (ver
// gattc_transport_write/wait, registradas em app_map_set_transport() logo
// abaixo, chamadas de qualquer task via fila — NUNCA da própria task do
// host NimBLE, senão trava esperando a si mesma).
// ─────────────────────────────────────────────────────

static const ble_uuid128_t s_gattc_svc_uuid      = BLE_UUID128_INIT(APP_MAP_BLE_SVC_UUID128);
static const ble_uuid128_t s_gattc_chr_telem_uuid = BLE_UUID128_INIT(APP_MAP_BLE_CHR_TELEMETRIA_UUID128);
static const ble_uuid128_t s_gattc_chr_req_uuid   = BLE_UUID128_INIT(APP_MAP_BLE_CHR_UDS_REQ_UUID128);
static const ble_uuid128_t s_gattc_chr_resp_uuid  = BLE_UUID128_INIT(APP_MAP_BLE_CHR_UDS_RESP_UUID128);

typedef enum {
    GATTC_CHR_TELEMETRIA = 0,
    GATTC_CHR_UDS_REQ    = 1,
    GATTC_CHR_UDS_RESP   = 2,
} gattc_chr_which_t;

typedef struct {
    uint16_t def_handle;
    uint16_t val_handle;
    uint8_t  which;
} gattc_chr_found_t;

typedef struct {
    uint8_t len;
    uint8_t data[APP_MAP_PDU_MAX_LEN];
} uds_resp_item_t;

// volatile: escritas na task do host NimBLE (BLE_GAP_EVENT_*), leituras em
// qualquer outra task via gattc_transport_write/wait (ex.: a task da UI ao
// salvar um mapa) — mesmo motivo do 's_ready' la em cima, sao os dois
// "flags de publicacao" que atravessam nucleos (NimBLE fixado no core 0
// via CONFIG_NIMBLE_PINNED_TO_CORE_0).
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool     s_gattc_ready = false;
static uint16_t           s_svc_start_handle;
static uint16_t           s_svc_end_handle;
static gattc_chr_found_t  s_chrs[3];
static int                s_chr_count;
static uint16_t           s_telem_val_handle;
static uint16_t           s_uds_req_val_handle;
static uint16_t           s_uds_resp_val_handle;
static int                s_cccd_step;          // 0 = telemetria, 1 = uds_resp, 2 = pronto
static bool                s_cccd_found_this_step;
static QueueHandle_t      s_uds_resp_queue;

static int gattc_on_write_cccd(uint16_t conn_handle, const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg);
static int gattc_on_disc_dsc(uint16_t conn_handle, const struct ble_gatt_error *error,
                              uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
static void gattc_cccd_step_done(uint16_t conn_handle);

static uint16_t gattc_chr_end_handle(uint16_t def_handle)
{
    uint16_t next_def = 0;
    for (int i = 0; i < s_chr_count; i++) {
        if (s_chrs[i].def_handle > def_handle) {
            if (next_def == 0 || s_chrs[i].def_handle < next_def) {
                next_def = s_chrs[i].def_handle;
            }
        }
    }
    return next_def ? (uint16_t)(next_def - 1) : s_svc_end_handle;
}

static void gattc_mark_ready(void)
{
    // A cadeia de discovery e assincrona (varias trocas HCI em sequencia);
    // se a ECU desconectar no meio dela, BLE_GAP_EVENT_DISCONNECT ja rodou
    // gattc_reset_state() e zerou s_conn_handle antes deste callback final
    // chegar — sem este guard, marcariamos "pronto" uma conexao que ja
    // caiu (proximo write so falharia rc!=0, mas o estado ficaria errado
    // ate isso acontecer).
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    s_gattc_ready = true;
    ESP_LOGI(TAG, "GATT client pronto — servico da ECU descoberto, notificacoes ativadas");
    app_ecu_set_link_state(APP_ECU_STATE_CONNECTED);
}

static void gattc_next_cccd(uint16_t conn_handle)
{
    int which = (s_cccd_step == 0) ? GATTC_CHR_TELEMETRIA : GATTC_CHR_UDS_RESP;
    s_cccd_found_this_step = false;

    for (int i = 0; i < s_chr_count; i++) {
        if (s_chrs[i].which == (uint8_t)which) {
            uint16_t end = gattc_chr_end_handle(s_chrs[i].def_handle);
            int rc = ble_gattc_disc_all_dscs(conn_handle, s_chrs[i].val_handle, end,
                                              gattc_on_disc_dsc, (void *)(uintptr_t)which);
            if (rc != 0) {
                ESP_LOGW(TAG, "disc_all_dscs falhou (which=%d): rc=%d", which, rc);
                gattc_cccd_step_done(conn_handle);
            }
            return;
        }
    }
    // Characteristic de notify correspondente nao existe nesta ECU —
    // segue sem ativar notificacao pra ela (nao trava o resto do fluxo).
    ESP_LOGW(TAG, "characteristic notify %d nao encontrada — pulando", which);
    gattc_cccd_step_done(conn_handle);
}

static void gattc_cccd_step_done(uint16_t conn_handle)
{
    s_cccd_step++;
    if (s_cccd_step >= 2) {
        gattc_mark_ready();
    } else {
        gattc_next_cccd(conn_handle);
    }
}

static int gattc_on_write_cccd(uint16_t conn_handle, const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    int which = (int)(uintptr_t)arg;
    if (error->status != 0) {
        ESP_LOGW(TAG, "falha ao ativar notificacao (which=%d): status=%d", which, error->status);
    } else {
        ESP_LOGI(TAG, "[GATTC] notificacao ativada (which=%d)", which);
    }
    gattc_cccd_step_done(conn_handle);
    return 0;
}

static int gattc_on_disc_dsc(uint16_t conn_handle, const struct ble_gatt_error *error,
                              uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle;
    int which = (int)(uintptr_t)arg;

    if (error->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(0x2902)) == 0) {
            s_cccd_found_this_step = true;
            static const uint8_t enable[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, enable, sizeof(enable),
                                           gattc_on_write_cccd, (void *)(uintptr_t)which);
            if (rc != 0) {
                ESP_LOGW(TAG, "write CCCD falhou (which=%d): rc=%d", which, rc);
                gattc_cccd_step_done(conn_handle);
            }
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_cccd_found_this_step) {
            ESP_LOGW(TAG, "CCCD nao encontrado (which=%d) — notificacao nao ativada", which);
            gattc_cccd_step_done(conn_handle);
        }
        return 0;
    }
    return 0;
}

static int gattc_on_disc_all_chrs(uint16_t conn_handle, const struct ble_gatt_error *error,
                                   const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == 0 && chr) {
        int which = -1;
        if (ble_uuid_cmp(&chr->uuid.u, &s_gattc_chr_telem_uuid.u) == 0) which = GATTC_CHR_TELEMETRIA;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_gattc_chr_req_uuid.u) == 0) which = GATTC_CHR_UDS_REQ;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_gattc_chr_resp_uuid.u) == 0) which = GATTC_CHR_UDS_RESP;

        if (which >= 0 && s_chr_count < (int)(sizeof(s_chrs) / sizeof(s_chrs[0]))) {
            s_chrs[s_chr_count].def_handle = chr->def_handle;
            s_chrs[s_chr_count].val_handle = chr->val_handle;
            s_chrs[s_chr_count].which      = (uint8_t)which;
            s_chr_count++;

            if (which == GATTC_CHR_TELEMETRIA) s_telem_val_handle    = chr->val_handle;
            if (which == GATTC_CHR_UDS_REQ)    s_uds_req_val_handle  = chr->val_handle;
            if (which == GATTC_CHR_UDS_RESP)   s_uds_resp_val_handle = chr->val_handle;

            static const char *k_which_name[] = {"Telemetria", "UDS Request", "UDS Response"};
            ESP_LOGI(TAG, "[GATTC] characteristic %s encontrada (val_handle=%u)",
                     k_which_name[which], chr->val_handle);
        } else {
            ESP_LOGW(TAG, "[GATTC] characteristic desconhecida no servico (handle=%u) — ignorada",
                     chr->val_handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_chr_count == 0) {
            ESP_LOGW(TAG, "nenhuma characteristic conhecida encontrada no servico da ECU");
            return 0;
        }
        ESP_LOGI(TAG, "[GATTC] %d characteristic(s) conhecida(s) encontrada(s) — ativando notificacoes",
                 s_chr_count);
        s_cccd_step = 0;
        gattc_next_cccd(conn_handle);
        return 0;
    }
    ESP_LOGW(TAG, "descoberta de characteristics falhou: status=%d", error->status);
    return 0;
}

static int gattc_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (error->status == 0 && service) {
        s_svc_start_handle = service->start_handle;
        s_svc_end_handle   = service->end_handle;
        ESP_LOGI(TAG, "[GATTC] servico da ECU encontrado (handles %u..%u)",
                 s_svc_start_handle, s_svc_end_handle);
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_svc_start_handle == 0) {
            ESP_LOGW(TAG, "servico da ECU nao encontrado nesse periferico (UUID nao bate)");
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(conn_handle, s_svc_start_handle, s_svc_end_handle,
                                          gattc_on_disc_all_chrs, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "disc_all_chrs falhou: rc=%d", rc);
        }
        return 0;
    }
    ESP_LOGW(TAG, "descoberta de servico falhou: status=%d", error->status);
    return 0;
}

static void gattc_reset_state(void)
{
    s_gattc_ready         = false;
    s_svc_start_handle    = 0;
    s_svc_end_handle      = 0;
    s_chr_count           = 0;
    memset(s_chrs, 0, sizeof(s_chrs));
    s_telem_val_handle    = 0;
    s_uds_req_val_handle  = 0;
    s_uds_resp_val_handle = 0;
    s_cccd_step           = 0;
    if (s_uds_resp_queue) {
        xQueueReset(s_uds_resp_queue);
    }
}

// --- Transporte pro protocolo de mapas (app_map.c), ver app_map_set_transport() ---
// Chamadas de QUALQUER task (tipicamente a task da UI/LVGL, quando o
// usuario clica "Salvar Mapa") — NUNCA da task do host NimBLE, que so
// ENTREGA a resposta nesta fila (BLE_GAP_EVENT_NOTIFY_RX abaixo).

static esp_err_t gattc_transport_write(const uint8_t *pdu, uint8_t len)
{
    if (!s_gattc_ready || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_uds_req_val_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    // Descarta qualquer notify velho antes de comecar uma transacao nova
    // (protocolo e estritamente sequencial: um pedido, uma resposta).
    xQueueReset(s_uds_resp_queue);

    int rc = ble_gattc_write_flat(s_conn_handle, s_uds_req_val_handle, pdu, len, NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gattc_write_flat (UDS Request) falhou: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t gattc_transport_wait(uint8_t *out, uint8_t *out_len, uint32_t timeout_ms)
{
    if (!s_gattc_ready) return ESP_ERR_INVALID_STATE;

    uds_resp_item_t item;
    if (xQueueReceive(s_uds_resp_queue, &item, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(out, item.data, item.len);
    *out_len = item.len;
    return ESP_OK;
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "reset (motivo=%d)", reason);
    s_ready = false;
    set_status(APP_BLE_STATE_IDLE, "Reiniciado");
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "host NimBLE sincronizado");
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_ready = true;
    set_status(APP_BLE_STATE_IDLE, "Pronto");

    app_map_debug_ble_start_advertising();  // DEBUG TEMPORARIO
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void handle_disc(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);

    xSemaphoreTake(s_lock, portMAX_DELAY);

    int idx = -1;
    for (uint32_t i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan_results[i].addr, disc->addr.val, 6) == 0) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0 && s_scan_count < APP_BLE_MAX_SCAN_RESULTS) {
        idx = (int)s_scan_count++;
        memset(&s_scan_results[idx], 0, sizeof(s_scan_results[idx]));
        memcpy(s_scan_results[idx].addr, disc->addr.val, 6);
        s_scan_results[idx].addr_type = disc->addr.type;
        snprintf(s_scan_results[idx].addr_str, sizeof(s_scan_results[idx].addr_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                 disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
        strcpy(s_scan_results[idx].name, "(sem nome)");
    }
    if (idx >= 0) {
        s_scan_results[idx].rssi = disc->rssi;
        if (fields.name_len > 0) {
            uint8_t n = fields.name_len < sizeof(s_scan_results[idx].name) - 1
                        ? fields.name_len : sizeof(s_scan_results[idx].name) - 1;
            memcpy(s_scan_results[idx].name, fields.name, n);
            s_scan_results[idx].name[n] = '\0';
        }
    }

    xSemaphoreGive(s_lock);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_disc(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        set_status(APP_BLE_STATE_IDLE, "Scan concluido");
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "conectado (handle=%d) — iniciando descoberta do servico da ECU",
                     (int)event->connect.conn_handle);
            set_status(APP_BLE_STATE_CONNECTED, "Conectado");
            s_conn_handle = event->connect.conn_handle;
            gattc_reset_state();
            app_ecu_set_link_state(APP_ECU_STATE_CONNECTING);

            int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_gattc_svc_uuid.u,
                                                 gattc_on_disc_svc, NULL);
            if (rc != 0) {
                ESP_LOGW(TAG, "disc_svc_by_uuid falhou: rc=%d", rc);
            }
        } else {
            ESP_LOGW(TAG, "falha ao conectar: status=%d", event->connect.status);
            set_status(APP_BLE_STATE_ERROR, "Falha ao conectar");
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "desconectado (motivo=%d)", event->disconnect.reason);
        set_status(APP_BLE_STATE_IDLE, "Desconectado");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        gattc_reset_state();
        app_ecu_set_link_state(APP_ECU_STATE_DISCONNECTED);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t mlen = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (mlen > APP_MAP_PDU_MAX_LEN) mlen = APP_MAP_PDU_MAX_LEN;
        uint8_t buf[APP_MAP_PDU_MAX_LEN];
        os_mbuf_copydata(event->notify_rx.om, 0, mlen, buf);

        if (s_uds_resp_val_handle != 0 && event->notify_rx.attr_handle == s_uds_resp_val_handle) {
            uds_resp_item_t item;
            item.len = (uint8_t)mlen;
            memcpy(item.data, buf, mlen);
            xQueueOverwrite(s_uds_resp_queue, &item);
        } else if (s_telem_val_handle != 0 && event->notify_rx.attr_handle == s_telem_val_handle) {
            app_ecu_feed_ble_notify(buf, mlen);
        }
        return 0;
    }

    default:
        return 0;
    }
}

esp_err_t app_ble_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_uds_resp_queue = xQueueCreate(1, sizeof(uds_resp_item_t));
    if (!s_uds_resp_queue) {
        return ESP_ERR_NO_MEM;
    }

    // O nivel de log do host NimBLE ja e limitado via
    // CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING (sdkconfig.defaults); isso evita
    // o spam continuo de logs de advertising/scan no monitor serial.
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init falhou: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb  = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gap_device_name_set("ZOTTI-ECU");

    app_map_debug_ble_register_gatt();  // DEBUG TEMPORARIO — precisa vir antes do host comecar a rodar

    // Registra o transporte GATT client pro protocolo de mapas — as
    // funcoes checam sozinhas se ha ECU conectada/pronta (ESP_ERR_INVALID_STATE
    // caso nao), entao e seguro registrar sempre, mesmo sem nada plugado.
    app_map_set_transport(gattc_transport_write, gattc_transport_wait);

    set_status(APP_BLE_STATE_IDLE, "Inicializando...");

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool app_ble_is_ready(void)
{
    return s_ready;
}

esp_err_t app_ble_scan_start(void)
{
    if (!s_ready) {
        set_status(APP_BLE_STATE_ERROR, "BLE nao sincronizado");
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_gap_disc_active()) {
        return ESP_OK;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_count = 0;
    xSemaphoreGive(s_lock);

    struct ble_gap_disc_params params = {0};
    params.passive = 1;
    params.filter_duplicates = 0;

    int rc = ble_gap_disc(s_own_addr_type, 10000, &params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc falhou: %d", rc);
        set_status(APP_BLE_STATE_ERROR, "Falha ao iniciar scan");
        return ESP_FAIL;
    }

    set_status(APP_BLE_STATE_SCANNING, "Escaneando...");
    return ESP_OK;
}

void app_ble_scan_stop(void)
{
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }
    set_status(APP_BLE_STATE_IDLE, "Scan parado");
}

uint32_t app_ble_get_scan_results(app_ble_scan_result_t *out, uint32_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t n = s_scan_count < max ? s_scan_count : max;
    memcpy(out, s_scan_results, n * sizeof(app_ble_scan_result_t));
    xSemaphoreGive(s_lock);
    return n;
}

esp_err_t app_ble_connect(const app_ble_scan_result_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }

    ble_addr_t addr;
    addr.type = dev->addr_type;
    memcpy(addr.val, dev->addr, 6);

    set_status(APP_BLE_STATE_CONNECTING, "Conectando...");

    int rc = ble_gap_connect(s_own_addr_type, &addr, 5000, NULL, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect falhou: %d", rc);
        set_status(APP_BLE_STATE_ERROR, "Falha ao conectar");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void app_ble_get_status(app_ble_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
