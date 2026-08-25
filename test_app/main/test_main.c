#include "unity.h"

// Ponto de entrada do app de teste. Cada TEST_CASE(...) espalhado pelos
// arquivos test_*.c deste diretório se auto-registra (macro do componente
// "unity" do ESP-IDF) — unity_run_all_tests() roda todos, sem precisar
// listar um por um aqui.
void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
