#include "app_ble.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "APP_BLE";
static volatile bool s_ready = false;

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "reset (motivo=%d)", reason);
    s_ready = false;
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "host NimBLE sincronizado");
    s_ready = true;
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t app_ble_init(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init falhou: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb  = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gap_device_name_set("ZOTTI-ECU");

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool app_ble_is_ready(void)
{
    return s_ready;
}
