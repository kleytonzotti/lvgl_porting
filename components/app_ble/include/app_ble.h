#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_BLE_MAX_SCAN_RESULTS 20

typedef enum {
    APP_BLE_STATE_IDLE = 0,
    APP_BLE_STATE_SCANNING,
    APP_BLE_STATE_CONNECTING,
    APP_BLE_STATE_CONNECTED,
    APP_BLE_STATE_ERROR,
} app_ble_state_t;

// One discovered peer (snapshot copy — safe to call from any task).
typedef struct {
    char    name[32];
    uint8_t addr[6];
    uint8_t addr_type;
    char    addr_str[18];   // "XX:XX:XX:XX:XX:XX"
    int8_t  rssi;
} app_ble_scan_result_t;

typedef struct {
    app_ble_state_t state;
    char            status_text[64];
} app_ble_status_t;

// --- Core lifecycle ---
esp_err_t app_ble_init(void);
bool      app_ble_is_ready(void);

// --- Scan (snapshot copy — safe to call from any task) ---
esp_err_t app_ble_scan_start(void);
void      app_ble_scan_stop(void);
uint32_t  app_ble_get_scan_results(app_ble_scan_result_t *out, uint32_t max);

// --- Connect to a peer found by the scan above ---
esp_err_t app_ble_connect(const app_ble_scan_result_t *dev);

// --- Status ---
void app_ble_get_status(app_ble_status_t *out);

#ifdef __cplusplus
}
#endif
