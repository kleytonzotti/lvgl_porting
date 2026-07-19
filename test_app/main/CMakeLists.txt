idf_component_register(
    SRCS
        "test_main.c"
        "test_app_ecu.c"
        "test_app_pedal_link.c"
        "test_app_sim.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        unity
        app_ecu
        app_pedal_link
        app_sim
)
