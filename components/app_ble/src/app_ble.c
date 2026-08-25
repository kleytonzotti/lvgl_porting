#include "app_ble.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

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
            set_status(APP_BLE_STATE_CONNECTED, "Conectado");
        } else {
            set_status(APP_BLE_STATE_ERROR, "Falha ao conectar");
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        set_status(APP_BLE_STATE_IDLE, "Desconectado");
        return 0;

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
