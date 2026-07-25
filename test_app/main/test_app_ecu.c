#include "unity.h"
#include "app_ecu.h"

// Monta um frame v1 valido a partir dos campos ja convertidos, calculando
// o checksum do mesmo jeito que app_ecu_feed_ble_notify espera (XOR dos
// bytes [0..3+N-1]). Ver components/app_ecu/include/app_ecu.h pro formato.
static void build_frame(uint8_t *out, uint16_t rpm, uint8_t map_kpa, uint8_t tps_pct,
                         int8_t ect_c, int8_t iat_c, uint16_t batt_mv,
                         uint16_t lambda_x1000, uint32_t uptime_ms)
{
    out[0] = APP_ECU_FRAME_START;
    out[1] = APP_ECU_PROTO_VERSION;
    out[2] = APP_ECU_PAYLOAD_V1_LEN;

    out[3] = (uint8_t)(rpm & 0xFF);
    out[4] = (uint8_t)((rpm >> 8) & 0xFF);
    out[5] = map_kpa;
    out[6] = tps_pct;
    out[7] = (uint8_t)ect_c;
    out[8] = (uint8_t)iat_c;
    out[9]  = (uint8_t)(batt_mv & 0xFF);
    out[10] = (uint8_t)((batt_mv >> 8) & 0xFF);
    out[11] = (uint8_t)(lambda_x1000 & 0xFF);
    out[12] = (uint8_t)((lambda_x1000 >> 8) & 0xFF);
    out[13] = (uint8_t)(uptime_ms & 0xFF);
    out[14] = (uint8_t)((uptime_ms >> 8) & 0xFF);
    out[15] = (uint8_t)((uptime_ms >> 16) & 0xFF);
    out[16] = (uint8_t)((uptime_ms >> 24) & 0xFF);

    uint8_t chk = 0;
    for (int i = 0; i < 3 + APP_ECU_PAYLOAD_V1_LEN; i++) chk ^= out[i];
    out[17] = chk;
}

TEST_CASE("app_ecu: frame valido decodifica todos os campos corretamente", "[app_ecu]")
{
    app_ecu_init();

    uint8_t frame[APP_ECU_FRAME_MAX_LEN];
    build_frame(frame, 3500, 85, 42, 88, 27, 13850, 1000, 12345);

    app_ecu_feed_ble_notify(frame, sizeof(frame));

    app_ecu_data_t d;
    app_ecu_get_data(&d);

    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL_UINT16(3500, d.rpm);
    TEST_ASSERT_EQUAL_UINT8(85, d.map_kpa);
    TEST_ASSERT_EQUAL_UINT8(42, d.tps_pct);
    TEST_ASSERT_EQUAL_INT8(88, d.ect_c);
    TEST_ASSERT_EQUAL_INT8(27, d.iat_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.850f, d.batt_v);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.000f, d.lambda);
    TEST_ASSERT_EQUAL_UINT32(12345, d.ecu_uptime_ms);

    app_ecu_status_t st;
    app_ecu_get_status(&st);
    TEST_ASSERT_EQUAL(APP_ECU_STATE_CONNECTED, st.state);
}

TEST_CASE("app_ecu: checksum invalido e rejeitado (nao conta como frame ok)", "[app_ecu]")
{
    app_ecu_init();

    uint8_t frame[APP_ECU_FRAME_MAX_LEN];
    build_frame(frame, 4000, 60, 30, 85, 25, 14000, 980, 500);
    frame[17] ^= 0xFF;   // corrompe soh o checksum

    app_ecu_status_t before;
    app_ecu_get_status(&before);

    app_ecu_feed_ble_notify(frame, sizeof(frame));

    app_ecu_status_t after;
    app_ecu_get_status(&after);
    TEST_ASSERT_EQUAL_UINT32(before.frames_bad_checksum + 1, after.frames_bad_checksum);
    TEST_ASSERT_EQUAL_UINT32(before.frames_ok, after.frames_ok);
}

TEST_CASE("app_ecu: versao de protocolo desconhecida e rejeitada", "[app_ecu]")
{
    app_ecu_init();

    uint8_t frame[APP_ECU_FRAME_MAX_LEN];
    build_frame(frame, 1000, 30, 0, 20, 20, 12000, 1000, 0);
    frame[1] = 0x02;   // versao que o parser nao conhece

    app_ecu_status_t before;
    app_ecu_get_status(&before);

    app_ecu_feed_ble_notify(frame, sizeof(frame));

    app_ecu_status_t after;
    app_ecu_get_status(&after);
    TEST_ASSERT_EQUAL_UINT32(before.frames_bad_len + 1, after.frames_bad_len);
}

TEST_CASE("app_ecu: frame truncado (menor que o esperado) e rejeitado", "[app_ecu]")
{
    app_ecu_init();

    uint8_t frame[APP_ECU_FRAME_MAX_LEN];
    build_frame(frame, 2500, 50, 20, 80, 24, 13700, 1000, 999);

    app_ecu_status_t before;
    app_ecu_get_status(&before);

    // Manda so os primeiros 10 bytes do frame de 18 — corrompido/incompleto.
    app_ecu_feed_ble_notify(frame, 10);

    app_ecu_status_t after;
    app_ecu_get_status(&after);
    TEST_ASSERT_EQUAL_UINT32(before.frames_bad_len + 1, after.frames_bad_len);
}

TEST_CASE("app_ecu: desconectar invalida o ultimo dado recebido", "[app_ecu]")
{
    app_ecu_init();

    uint8_t frame[APP_ECU_FRAME_MAX_LEN];
    build_frame(frame, 2000, 40, 10, 70, 25, 13500, 1000, 100);
    app_ecu_feed_ble_notify(frame, sizeof(frame));

    app_ecu_data_t d;
    app_ecu_get_data(&d);
    TEST_ASSERT_TRUE(d.valid);

    app_ecu_set_link_state(APP_ECU_STATE_DISCONNECTED);
    app_ecu_get_data(&d);
    TEST_ASSERT_FALSE(d.valid);
}
