#include "unity.h"
#include "app_sim.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

TEST_CASE("app_sim: desligado por padrao nao atualiza dados", "[app_sim]")
{
    app_sim_init();
    app_sim_set_enabled(false);

    app_sim_data_t before, after;
    app_sim_get_data(&before);
    vTaskDelay(pdMS_TO_TICKS(300));
    app_sim_get_data(&after);

    // Com o demo desligado, o snapshot nao deve mudar (a task continua
    // rodando, mas so publica valores quando app_sim_is_enabled() é true).
    TEST_ASSERT_EQUAL_UINT16(before.rpm, after.rpm);
}

TEST_CASE("app_sim: ligado, os valores ficam dentro de faixas fisicamente plausiveis", "[app_sim]")
{
    app_sim_init();
    app_sim_set_redline(7000);
    app_sim_set_enabled(true);

    app_sim_data_t initial;
    app_sim_get_data(&initial);
    bool saw_motion = false;

    // Da tempo de rodar varios ticks (a task roda a cada 100ms) cobrindo
    // fases diferentes do ciclo simulado.
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(120));

        app_sim_data_t d;
        app_sim_get_data(&d);

        if (d.rpm > initial.rpm + 300 || d.speed_kph > 5) saw_motion = true;

        TEST_ASSERT_LESS_OR_EQUAL_UINT16(7000, d.rpm);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(220, d.speed_kph);   // teto interno do gerador (ver app_sim.c)
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, d.throttle_pct);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, d.tps_pct);
        TEST_ASSERT_TRUE(d.map_kpa >= 15 && d.map_kpa <= 105);
        TEST_ASSERT_TRUE(d.batt_v > 12.0f && d.batt_v < 15.5f);
        TEST_ASSERT_TRUE(d.lambda > 0.7f && d.lambda < 1.3f);
        TEST_ASSERT_TRUE(d.accel_g > -1.5f && d.accel_g < 1.5f);
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_motion, "Demo nao saiu da marcha lenta");

    app_sim_set_enabled(false);
}

TEST_CASE("app_sim: redline configurado e respeitado", "[app_sim]")
{
    app_sim_set_redline(9000);
    TEST_ASSERT_EQUAL_UINT16(9000, app_sim_get_redline());

    // redline_rpm == 0 deve ser ignorado (nao faz sentido um corte em 0).
    app_sim_set_redline(0);
    TEST_ASSERT_EQUAL_UINT16(9000, app_sim_get_redline());
}
