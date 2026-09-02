#include "app_map_debug_ble.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "services/gatt/ble_svc_gatt.h"

#include "app_map.h"

static const char *TAG = "APP_MAP_DBG_BLE";

// Mesmo nome ja configurado em app_ble.c (ble_svc_gap_device_name_set) —
// e o nome pra filtrar no nRF Connect.
#define DEBUG_DEVICE_NAME "ZOTTI-ECU"

// UUIDs identicos aos documentados em STM32_ECU_SIMULADOR_BLE.md — definidos
// uma unica vez em app_map.h (compartilhados com o GATT client de verdade
// em components/app_ble) pra nunca divergir entre servidor de debug e
// cliente real.
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(APP_MAP_BLE_SVC_UUID128);

static const ble_uuid128_t s_chr_uds_req_uuid = BLE_UUID128_INIT(APP_MAP_BLE_CHR_UDS_REQ_UUID128);

static uint16_t s_chr_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static int debug_gap_event(struct ble_gap_event *event, void *arg);

// Incidente real (2026-09-01): conectar com nRF Connect travava logo apos
// "conectado" com "BLE_INIT: Malloc failed" no log — causa era o MTU
// preferido do ATT (256, padrao de fabrica do ESP-IDF) forcando uma
// alocacao de buffer grande demais pra RAM interna livre desta placa (ver
// sdkconfig.defaults). Corrigido reduzindo o MTU preferido pra 23 (o
// minimo do BLE — sobra de sobra pros PDUs daqui, que tem no maximo 18
// bytes). Este log fica pra confirmar headroom se algo parecido
// acontecer de novo.
static void log_heap_diag(const char *when)
{
    ESP_LOGI(TAG, "[DIAG-BLE-MEM] %s: heap interno livre=%u bytes (maior bloco=%u)",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// Characteristic e so NOTIFY (sem READ/WRITE) — este callback nunca
// deveria ser chamado de verdade, so existe porque o struct exige um
// ponteiro de funcao.
static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &s_chr_uds_req_uuid.u,
                .access_cb  = gatt_access_cb,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_chr_val_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

// Chamado por app_map_send_to_ecu() (via app_map_set_debug_sniffer) pra
// CADA PDU que seria transmitido de verdade — so notifica se tiver
// alguem conectado (custo zero na operacao normal, sem ninguem plugado).
static void sniffer_notify_cb(void *ctx, const uint8_t *pdu, uint8_t len)
{
    (void)ctx;

    // Verificacao sem depender do nRF Connect/BLE funcionar: o mesmo PDU
    // que seria notificado tambem sai em hexadecimal no log serial, sempre
    // (mesmo sem ninguem conectado). Primeiro byte = SID UDS (0x10 sessao,
    // 0x27 seguranca, 0x34 RequestDownload, 0x36 TransferData, 0x37
    // RequestTransferExit — ver app_map.h).
    char hex[3 * APP_MAP_PDU_MAX_LEN + 1];
    int pos = 0;
    for (uint8_t i = 0; i < len && pos < (int)sizeof(hex) - 3; i++) {
        pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", pdu[i]);
    }
    ESP_LOGI(TAG, "[DEBUG-BLE] PDU (len=%u): %s", len, hex);

    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pdu, len);
    if (!om) return;

    int rc = ble_gatts_notify_custom(s_conn_handle, s_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify falhou (rc=%d) — cliente esta com notify ativado?", rc);
    }
    // Folga entre PDUs consecutivos, pra nao atropelar o buffer de TX do
    // controlador BLE numa rajada de ~13 notificacoes seguidas.
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void restart_advertising(void)
{
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        ESP_LOGW(TAG, "ble_hs_id_infer_auto falhou — host ainda nao tem endereco proprio");
        return;
    }

    struct ble_hs_adv_fields fields = {0};
    fields.flags         = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name          = (const uint8_t *)DEBUG_DEVICE_NAME;
    fields.name_len      = (uint8_t)strlen(DEBUG_DEVICE_NAME);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                                debug_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_start falhou: %d", rc);
    } else {
        ESP_LOGI(TAG, "[DEBUG-BLE] anunciando como \"%s\" — procure este nome no nRF Connect",
                 DEBUG_DEVICE_NAME);
    }
}

static int debug_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "[DEBUG-BLE] conectado (handle=%d)", (int)s_conn_handle);
            log_heap_diag("apos conectar");
        } else {
            ESP_LOGW(TAG, "[DEBUG-BLE] falha ao conectar (status=%d)", event->connect.status);
            restart_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "[DEBUG-BLE] desconectado (motivo=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        log_heap_diag("apos desconectar");
        restart_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "[DEBUG-BLE] subscribe: attr=%d reason=%d",
                 (int)event->subscribe.attr_handle, (int)event->subscribe.reason);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        restart_advertising();
        return 0;

    default:
        return 0;
    }
}

esp_err_t app_map_debug_ble_register_gatt(void)
{
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "count_cfg falhou: %d", rc); return ESP_FAIL; }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "add_svcs falhou: %d", rc); return ESP_FAIL; }

    rc = ble_gatts_start();
    if (rc != 0) { ESP_LOGE(TAG, "gatts_start falhou: %d", rc); return ESP_FAIL; }

    app_map_set_debug_sniffer(sniffer_notify_cb, NULL);
    ESP_LOGI(TAG, "[DEBUG-BLE] servico GATT de debug registrado (ver app_map_debug_ble.h)");
    return ESP_OK;
}

void app_map_debug_ble_start_advertising(void)
{
    restart_advertising();
}
