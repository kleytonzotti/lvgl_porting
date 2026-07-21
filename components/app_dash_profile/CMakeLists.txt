idf_component_register(
    SRCS
        "src/app_dash_profile.c"
        "src/app_dash_minmax.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        esp_timer
        nvs_flash
)
