idf_component_register(
    SRCS
        # Core
        "src/ui.c"
        "src/ui_splash.c"
        "src/ui_menu.c"

        # Telas principais
        "src/ui_screen_dashboard.c"
        "src/ui_screen_scanner.c"
        "src/ui_screen_ecu.c"
        "src/ui_screen_can.c"
        "src/ui_screen_can_sniffer.c"
        "src/ui_screen_datalogger.c"
        "src/ui_screen_config.c"
        "src/ui_screen_sistema.c"

        # Legado (mantidos para compatibilidade)
        "src/ui_tabs.c"
        "src/ui_screen_animacao.c"
        "src/ui_screen_entradas.c"
        "src/ui_screen_saidas.c"
        "src/ui_screen_calculos.c"
        "src/ui_screen_botoes.c"
        "src/ui_screen_touch.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bsp_waveshare_43
        app_io
        app_core
        app_can
        lvgl__lvgl
        esp_common
        assets
)
