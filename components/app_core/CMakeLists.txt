idf_component_register(
    SRCS
        "src/app_core.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        app_io
        app_bcu
        app_ble
        app_ecu
        app_pedal_link
        app_sim
        app_dash_profile
        app_map
        nvs_flash
)
