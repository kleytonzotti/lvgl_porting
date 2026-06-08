idf_component_register(
    SRCS
        "src/app_inputs.c"
        "src/app_outputs.c"
        "src/app_4_20ma.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        driver
)