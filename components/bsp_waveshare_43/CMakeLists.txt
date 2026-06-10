idf_component_register(
    SRCS
        "src/bsp_waveshare_43.c"
        "src/bsp_i2c.c"
        "src/bsp_backlight.c"
        "src/bsp_lcd_rgb.c"
        "src/bsp_lvgl_port.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        driver
        esp_lcd
        esp_timer
        lvgl__lvgl
        espressif__esp_lcd_touch_gt911
        
        
)
