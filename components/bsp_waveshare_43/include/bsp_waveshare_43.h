#ifndef BSP_WAVESHARE_43_H
#define BSP_WAVESHARE_43_H

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Resolução ──────────────────────────────────────────────
#define BSP_LCD_H_RES               (800)
#define BSP_LCD_V_RES               (480)

// ── Clock do painel RGB ────────────────────────────────────
#define BSP_LCD_PIXEL_CLOCK_HZ      (16 * 1000 * 1000)

// ── Pinos RGB ─────────────────────────────────────────────
#define BSP_LCD_IO_VSYNC            (GPIO_NUM_3)
#define BSP_LCD_IO_HSYNC            (GPIO_NUM_46)
#define BSP_LCD_IO_DE               (GPIO_NUM_5)
#define BSP_LCD_IO_PCLK             (GPIO_NUM_7)
#define BSP_LCD_IO_DATA0            (GPIO_NUM_14)
#define BSP_LCD_IO_DATA1            (GPIO_NUM_38)
#define BSP_LCD_IO_DATA2            (GPIO_NUM_18)
#define BSP_LCD_IO_DATA3            (GPIO_NUM_17)
#define BSP_LCD_IO_DATA4            (GPIO_NUM_10)
#define BSP_LCD_IO_DATA5            (GPIO_NUM_39)
#define BSP_LCD_IO_DATA6            (GPIO_NUM_0)
#define BSP_LCD_IO_DATA7            (GPIO_NUM_45)
#define BSP_LCD_IO_DATA8            (GPIO_NUM_48)
#define BSP_LCD_IO_DATA9            (GPIO_NUM_47)
#define BSP_LCD_IO_DATA10           (GPIO_NUM_21)
#define BSP_LCD_IO_DATA11           (GPIO_NUM_1)
#define BSP_LCD_IO_DATA12           (GPIO_NUM_2)
#define BSP_LCD_IO_DATA13           (GPIO_NUM_42)
#define BSP_LCD_IO_DATA14           (GPIO_NUM_41)
#define BSP_LCD_IO_DATA15           (GPIO_NUM_40)

// ── I2C (backlight/touch via CH422G) ─────────────────────
#define BSP_I2C_NUM                 (0)
#define BSP_I2C_SDA                 (GPIO_NUM_8)
#define BSP_I2C_SCL                 (GPIO_NUM_9)
#define BSP_I2C_FREQ_HZ             (400000)
#define BSP_I2C_TIMEOUT_MS          (1000)

// ── LCD bounce buffer (linhas por transferência DMA) ─────
#define BSP_LCD_BOUNCE_LINES        (10)
#define BSP_LCD_BOUNCE_BUFFER_PX    (BSP_LCD_H_RES * BSP_LCD_BOUNCE_LINES)

// ── LVGL task ─────────────────────────────────────────────
#define BSP_LVGL_TICK_MS            (2)
#define BSP_LVGL_TASK_STACK_KB      (8)
#define BSP_LVGL_TASK_PRIORITY      (2)
#define BSP_LVGL_TASK_CORE          (1)
#define BSP_LVGL_TASK_MAX_DELAY_MS  (10)
#define BSP_LVGL_TASK_MIN_DELAY_MS  (1)
#define BSP_LVGL_BUFFER_LINES       (50)

esp_lcd_panel_handle_t bsp_lcd_get_panel_handle(void);

esp_err_t bsp_waveshare_43_init(void);

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_backlight_set(bool enable);
esp_err_t bsp_lcd_rgb_init(void);
esp_err_t bsp_lvgl_init(void);

bool bsp_lvgl_lock(int timeout_ms);
typedef void (*bsp_touch_cb_t)(lv_coord_t x, lv_coord_t y, bool pressed);
void bsp_lvgl_unlock(void);
void bsp_touch_register_cb(bsp_touch_cb_t cb);
void bsp_lvgl_handler(void);



#ifdef __cplusplus
}
#endif

#endif