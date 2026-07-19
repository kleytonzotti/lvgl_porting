idf_component_register(
    SRCS
        "src/app_dash_profile.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        nvs_flash
)
