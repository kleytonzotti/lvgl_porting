idf_component_register(
    SRCS
        "src/ui.c"
        "src/ui_splash.c"
        "src/ui_tabs.c"
        "src/ui_screen_animacao.c"
        "src/ui_screen_entradas.c"
        "src/ui_screen_saidas.c"
        "src/ui_screen_calculos.c"
        "src/ui_screen_botoes.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bsp_waveshare_43
        app_io
        app_core
)