#include "bsp_waveshare_43.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "BSP_LCD_RGB";
static esp_lcd_panel_handle_t s_panel_handle = NULL;

esp_lcd_panel_handle_t bsp_lcd_get_panel_handle(void)
{
    return s_panel_handle;
}

esp_err_t bsp_lcd_rgb_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src       = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz           = BSP_LCD_PIXEL_CLOCK_HZ,
            .h_res             = BSP_LCD_H_RES,
            .v_res             = BSP_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch  = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width            = 16,
        .bits_per_pixel        = 16,
        .num_fbs               = 1,
        .bounce_buffer_size_px = BSP_LCD_BOUNCE_BUFFER_PX,
        .sram_trans_align      = 4,
        .psram_trans_align     = 64,
        .hsync_gpio_num        = BSP_LCD_IO_HSYNC,
        .vsync_gpio_num        = BSP_LCD_IO_VSYNC,
        .de_gpio_num           = BSP_LCD_IO_DE,
        .pclk_gpio_num         = BSP_LCD_IO_PCLK,
        .disp_gpio_num         = -1,
        .data_gpio_nums = {
            BSP_LCD_IO_DATA0,  BSP_LCD_IO_DATA1,  BSP_LCD_IO_DATA2,
            BSP_LCD_IO_DATA3,  BSP_LCD_IO_DATA4,  BSP_LCD_IO_DATA5,
            BSP_LCD_IO_DATA6,  BSP_LCD_IO_DATA7,  BSP_LCD_IO_DATA8,
            BSP_LCD_IO_DATA9,  BSP_LCD_IO_DATA10, BSP_LCD_IO_DATA11,
            BSP_LCD_IO_DATA12, BSP_LCD_IO_DATA13, BSP_LCD_IO_DATA14,
            BSP_LCD_IO_DATA15,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel_handle),
                        TAG, "Falha ao criar painel RGB");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle),
                        TAG, "Falha ao inicializar painel RGB");

    ESP_LOGI(TAG, "LCD RGB inicializado (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}