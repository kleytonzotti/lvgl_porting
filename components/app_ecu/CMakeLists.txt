idf_component_register(
    SRCS
        "src/app_ecu.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        freertos
)
