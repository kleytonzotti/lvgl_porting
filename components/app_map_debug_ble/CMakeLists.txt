# DEBUG TEMPORARIO — ver app_map_debug_ble.h pro que isto faz e como
# remover quando nao precisar mais.
idf_component_register(
    SRCS
        "src/app_map_debug_ble.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        bt
        app_map
)
