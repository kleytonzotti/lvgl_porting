#include "app_ecu.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "APP_ECU";

static SemaphoreHandle_t  s_lock;
static app_ecu_data_t     s_data;
static app_ecu_status_t   s_status;

void app_ecu_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(&s_data, 0, sizeof(s_data));
    memset(&s_status, 0, sizeof(s_status));
    ESP_LOGI(TAG, "app_ecu pronto (aguardando link BLE)");
}

void app_ecu_set_link_state(app_ecu_link_state_t state)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    if (state != APP_ECU_STATE_CONNECTED) {
        s_data.valid = false;   // dado velho não deve continuar aparecendo como bom
    }
    xSemaphoreGive(s_lock);
}

// Payload v1 (14 bytes, little-endian):
//   [0-1]  uint16 rpm
//   [2]    uint8  map_kpa
//   [3]    uint8  tps_pct
//   [4]    int8   ect_c
//   [5]    int8   iat_c
//   [6-7]  uint16 batt_mv
//   [8-9]  uint16 lambda_x1000   (1000 == lambda 1.000)
//   [10-13] uint32 ecu_uptime_ms
static void decode_payload_v1(const uint8_t *p, app_ecu_data_t *out)
{
    out->rpm           = (uint16_t)(p[0] | (p[1] << 8));
    out->map_kpa        = p[2];
    out->tps_pct         = p[3];
    out->ect_c           = (int8_t)p[4];
    out->iat_c           = (int8_t)p[5];
    uint16_t batt_mv     = (uint16_t)(p[6] | (p[7] << 8));
    uint16_t lambda_x1000 = (uint16_t)(p[8] | (p[9] << 8));
    out->batt_v          = batt_mv / 1000.0f;
    out->lambda          = lambda_x1000 / 1000.0f;
    out->ecu_uptime_ms  = (uint32_t)p[10] | ((uint32_t)p[11] << 8) |
                          ((uint32_t)p[12] << 16) | ((uint32_t)p[13] << 24);
}

void app_ecu_feed_ble_notify(const uint8_t *data, size_t len)
{
    if (!s_lock || !data) return;

    if (len < 4 || data[0] != APP_ECU_FRAME_START || data[1] != APP_ECU_PROTO_VERSION) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.frames_bad_len++;
        xSemaphoreGive(s_lock);
        return;
    }

    uint8_t payload_len = data[2];
    size_t  frame_len    = (size_t)3 + payload_len + 1;
    if (payload_len != APP_ECU_PAYLOAD_V1_LEN || len < frame_len) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.frames_bad_len++;
        xSemaphoreGive(s_lock);
        return;
    }

    uint8_t checksum = 0;
    for (size_t i = 0; i < 3 + (size_t)payload_len; i++) {
        checksum ^= data[i];
    }
    if (checksum != data[3 + payload_len]) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.frames_bad_checksum++;
        xSemaphoreGive(s_lock);
        return;
    }

    app_ecu_data_t decoded = {0};
    decode_payload_v1(&data[3], &decoded);
    decoded.valid           = true;
    decoded.last_update_ms  = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data              = decoded;
    s_status.frames_ok++;
    s_status.state      = APP_ECU_STATE_CONNECTED;
    xSemaphoreGive(s_lock);
}

void app_ecu_get_data(app_ecu_data_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}

void app_ecu_get_status(app_ecu_status_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
