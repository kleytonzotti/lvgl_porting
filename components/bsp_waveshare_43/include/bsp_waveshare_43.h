#ifndef BSP_WAVESHARE_43_H
#define BSP_WAVESHARE_43_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_waveshare_43_init(void);

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_backlight_set(bool enable);
esp_err_t bsp_lcd_rgb_init(void);
esp_err_t bsp_lvgl_init(void);

void bsp_lvgl_handler(void);

#ifdef __cplusplus
}
#endif

#endif