#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t app_ble_init(void);
bool app_ble_is_ready(void);
