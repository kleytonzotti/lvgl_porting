#ifndef APP_CAN_H
#define APP_CAN_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CAN_SNIFF_LINE_MAX 96

typedef enum {
    APP_CAN_STATE_STOPPED = 0,
    APP_CAN_STATE_RUNNING,
    APP_CAN_STATE_ERROR,
} app_can_state_t;

typedef struct {
    app_can_state_t state;
    bool driver_ready;
    bool sd_mounted;
    bool log_open;
    uint32_t frames;
    uint32_t dropped_lines;
    uint32_t rx_errors;
    char last_error[64];
} app_can_status_t;

esp_err_t app_can_init(void);
esp_err_t app_can_sniffer_start(void);
void app_can_sniffer_stop(void);
bool app_can_sniffer_poll_line(char *out, size_t out_size);
void app_can_sniffer_get_status(app_can_status_t *out_status);
const char *app_can_sniffer_log_path(void);

#ifdef __cplusplus
}
#endif

#endif
