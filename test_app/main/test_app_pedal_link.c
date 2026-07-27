#include "unity.h"
#include "app_pedal_link.h"

// Importante: estes testes NUNCA chamam app_pedal_link_init() de proposito
// — essa funcao mexe em UART/GPIO de verdade (e os pinos usados hoje tem
// um conflito conhecido com o console de debug, ver bsp_waveshare_43.h).
// app_pedal_link_feed_bytes() exercita soh o parser, sem tocar em hardware.

TEST_CASE("app_pedal_link: telemetria valida atualiza o status", "[app_pedal_link]")
{
    uint8_t pedal_pct = 42, output_pct = 55, fault_flags = 0x00;
    uint8_t frame[4] = {
        APP_PEDAL_TELEMETRY_BYTE, pedal_pct, output_pct, fault_flags
    };
    uint8_t checksum = (uint8_t)(frame[0] ^ frame[1] ^ frame[2] ^ frame[3]);
    uint8_t full[5] = { frame[0], frame[1], frame[2], frame[3], checksum };

    app_pedal_status_t before;
    app_pedal_link_get_status(&before);

    app_pedal_link_feed_bytes(full, sizeof(full));

    app_pedal_status_t after;
    app_pedal_link_get_status(&after);

    TEST_ASSERT_TRUE(after.link_ok);
    TEST_ASSERT_EQUAL_UINT8(pedal_pct, after.pedal_pct);
    TEST_ASSERT_EQUAL_UINT8(output_pct, after.output_pct);
    TEST_ASSERT_EQUAL_UINT8(fault_flags, after.fault_flags);
    TEST_ASSERT_EQUAL_UINT32(before.frames_ok + 1, after.frames_ok);
}

TEST_CASE("app_pedal_link: checksum invalido nao conta como frame ok", "[app_pedal_link]")
{
    uint8_t full[5] = { APP_PEDAL_TELEMETRY_BYTE, 10, 20, 0x00, 0xFF /* checksum errado */ };

    app_pedal_status_t before;
    app_pedal_link_get_status(&before);

    app_pedal_link_feed_bytes(full, sizeof(full));

    app_pedal_status_t after;
    app_pedal_link_get_status(&after);

    TEST_ASSERT_EQUAL_UINT32(before.frames_bad_checksum + 1, after.frames_bad_checksum);
    TEST_ASSERT_EQUAL_UINT32(before.frames_ok, after.frames_ok);
}

TEST_CASE("app_pedal_link: bytes de lixo antes do start byte sao ignorados", "[app_pedal_link]")
{
    uint8_t pedal_pct = 7, output_pct = 9, fault_flags = 0x01;
    uint8_t checksum = (uint8_t)(APP_PEDAL_TELEMETRY_BYTE ^ pedal_pct ^ output_pct ^ fault_flags);

    // Lixo (nunca deveria disparar nada) seguido de um frame valido de verdade.
    uint8_t stream[9] = {
        0x11, 0x22, 0x33, 0x44, 0x55,
        APP_PEDAL_TELEMETRY_BYTE, pedal_pct, output_pct, fault_flags
    };
    // O checksum precisa vir logo depois — manda em duas partes pra simular
    // bytes chegando aos poucos pela UART, exatamente como a task real faz.
    app_pedal_link_feed_bytes(stream, sizeof(stream));
    app_pedal_link_feed_bytes(&checksum, 1);

    app_pedal_status_t after;
    app_pedal_link_get_status(&after);
    TEST_ASSERT_EQUAL_UINT8(pedal_pct, after.pedal_pct);
    TEST_ASSERT_EQUAL_UINT8(output_pct, after.output_pct);
}
