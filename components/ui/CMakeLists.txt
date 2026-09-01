idf_component_register(
    SRCS
        # Core
        "src/ui.c"
        "src/ui_splash.c"
        "src/ui_menu.c"
        "src/zotti_theme.c"

        # Telas principais
        "src/ui_screen_dashboard.c"
        "src/ui_screen_bcu_trip.c"
        "src/ui_screen_ecu.c"
        "src/ui_screen_can.c"
        "src/ui_screen_bcu_monitor.c"
        "src/ui_screen_sd_browser.c"
        "src/ui_screen_datalogger.c"
        "src/ui_screen_config.c"
        "src/ui_screen_sistema.c"
        "src/ui_screen_touch.c"
        "src/ui_screen_pedal.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bsp_waveshare_43
        app_io
        app_core
        app_bcu
        app_ble
        app_ecu
        app_sim
        app_dash_profile
        app_pedal_link
        lvgl__lvgl
        esp_common
        nvs_flash
        assets
)
