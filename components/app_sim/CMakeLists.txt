idf_component_register(
    SRCS
        "src/app_sim.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        esp_hw_support
        esp_timer
        freertos
)
