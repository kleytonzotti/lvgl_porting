idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        bsp_waveshare_43
        ui
        app_core
)