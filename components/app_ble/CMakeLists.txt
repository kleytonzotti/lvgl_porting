idf_component_register(
    SRCS
        "src/app_ble.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bt
        app_map_debug_ble
        app_map
        app_ecu
)