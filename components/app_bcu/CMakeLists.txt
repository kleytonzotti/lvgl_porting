idf_component_register(
    SRCS
        "src/app_bcu.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bsp_waveshare_43
        driver
        esp_common
        esp_driver_sdspi
        esp_driver_spi
        fatfs
        sdmmc
)
