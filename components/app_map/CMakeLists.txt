idf_component_register(
    SRCS
        "src/app_map.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        nvs_flash
        log
)
