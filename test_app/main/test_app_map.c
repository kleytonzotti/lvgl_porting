#include <string.h>
#include "unity.h"
#include "app_map.h"

// Testa só a lógica pura (framing/CRC/curva padrão) — sem tocar NVS de
// verdade, mesmo critério já usado pro resto do projeto (ver ROADMAP.md
// §11: "não incluí teste automatizado pra app_dash_profile, é CRUD fino
// sobre NVS, menor risco" — app_map_save_local/app_map_get seguem a mesma
// lógica de blob e ficam de fora daqui pelo mesmo motivo).

TEST_CASE("app_map: crc16 bate com o vetor de teste padrao CRC-16/CCITT-FALSE", "[app_map]")
{
    const uint8_t vec[] = "123456789";
    TEST_ASSERT_EQUAL_UINT16(0x29B1, app_map_crc16(vec, sizeof(vec) - 1));
}

TEST_CASE("app_map: reset_default preenche eixos certos e mantem celulas dentro dos limites", "[app_map]")
{
    app_map_set_t set;
    app_map_reset_default(&set);

    TEST_ASSERT_EQUAL_UINT16(800,  set.rpm_bins[0]);
    TEST_ASSERT_EQUAL_UINT16(7000, set.rpm_bins[APP_MAP_RPM_BINS - 1]);
    TEST_ASSERT_EQUAL_UINT16(20,   set.load_kpa_bins[0]);
    TEST_ASSERT_EQUAL_UINT16(100,  set.load_kpa_bins[APP_MAP_LOAD_BINS - 1]);

    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            TEST_ASSERT_TRUE(set.injecao[r][c] >= APP_MAP_INJ_MIN_TENTHS);
            TEST_ASSERT_TRUE(set.injecao[r][c] <= APP_MAP_INJ_MAX_TENTHS);
            TEST_ASSERT_TRUE(set.ignicao[r][c] >= APP_MAP_IGN_MIN_TENTHS);
            TEST_ASSERT_TRUE(set.ignicao[r][c] <= APP_MAP_IGN_MAX_TENTHS);
            TEST_ASSERT_TRUE(set.sonda[r][c] >= APP_MAP_SONDA_MIN_X100);
            TEST_ASSERT_TRUE(set.sonda[r][c] <= APP_MAP_SONDA_MAX_X100);
        }
    }
}

// Captura os pacotes que app_map_build_packets() vai gerando, na ordem.
typedef struct {
    uint8_t packets[32][APP_MAP_PACKET_MAX_LEN];
    uint8_t lens[32];
    int     count;
} pkt_capture_t;

static void capture_cb(void *ctx, const uint8_t *pkt, uint8_t len)
{
    pkt_capture_t *cap = (pkt_capture_t *)ctx;
    TEST_ASSERT_TRUE(cap->count < 32);
    memcpy(cap->packets[cap->count], pkt, len);
    cap->lens[cap->count] = len;
    cap->count++;
}

// Roda a mesma checagem de framing pra UMA tabela — chamada uma vez por
// table_id no TEST_CASE abaixo, assim uma tabela nova (ex.: quando a Sonda
// foi adicionada) ja fica coberta sem precisar duplicar o teste inteiro.
static void check_build_packets_for_table(const app_map_set_t *set, app_map_table_id_t tid)
{
    pkt_capture_t cap = {0};
    app_map_build_packets(set, tid, capture_cb, &cap);

    TEST_ASSERT_GREATER_OR_EQUAL(3, cap.count);   // BEGIN + >=1 CHUNK + END

    // --- MAP_BEGIN ---
    const uint8_t *begin = cap.packets[0];
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_FRAME_MARKER_CMD, begin[0]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_PROTO_VERSION, begin[1]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_MSG_BEGIN, begin[2]);
    TEST_ASSERT_EQUAL_UINT8(tid, begin[3]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_RPM_BINS, begin[4]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_LOAD_BINS, begin[5]);
    uint16_t total_len = (uint16_t)begin[6] | ((uint16_t)begin[7] << 8);
    TEST_ASSERT_EQUAL_UINT16(APP_MAP_SERIALIZED_LEN, total_len);
    uint8_t chk = 0;
    for (int i = 0; i < 8; i++) chk ^= begin[i];
    TEST_ASSERT_EQUAL_UINT8(chk, begin[8]);

    // --- MAP_CHUNK (reassembla e confere o checksum de cada pacote) ---
    uint8_t reassembled[APP_MAP_SERIALIZED_LEN] = {0};
    size_t  off      = 0;
    int     n_chunks = cap.count - 2;   // tira BEGIN e END
    for (int i = 0; i < n_chunks; i++) {
        const uint8_t *p   = cap.packets[1 + i];
        uint8_t        len = p[5];

        chk = 0;
        for (int b = 0; b < 6 + len; b++) chk ^= p[b];
        TEST_ASSERT_EQUAL_UINT8(chk, p[6 + len]);

        TEST_ASSERT_TRUE(off + len <= sizeof(reassembled));
        memcpy(&reassembled[off], &p[6], len);
        off += len;
    }
    TEST_ASSERT_EQUAL_UINT32(APP_MAP_SERIALIZED_LEN, off);

    uint8_t expected[APP_MAP_SERIALIZED_LEN];
    app_map_serialize_table(set, tid, expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, reassembled, APP_MAP_SERIALIZED_LEN);

    // --- MAP_END ---
    const uint8_t *end = cap.packets[cap.count - 1];
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_FRAME_MARKER_CMD, end[0]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_MSG_END, end[2]);
    TEST_ASSERT_EQUAL_UINT8(tid, end[3]);
    uint16_t crc = (uint16_t)end[4] | ((uint16_t)end[5] << 8);
    TEST_ASSERT_EQUAL_UINT16(app_map_crc16(reassembled, APP_MAP_SERIALIZED_LEN), crc);
    chk = 0;
    for (int i = 0; i < 6; i++) chk ^= end[i];
    TEST_ASSERT_EQUAL_UINT8(chk, end[6]);
}

TEST_CASE("app_map: build_packets monta BEGIN/CHUNK/END consistentes e reassembla igual ao serialize", "[app_map]")
{
    app_map_set_t set;
    app_map_reset_default(&set);

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        check_build_packets_for_table(&set, (app_map_table_id_t)t);
    }
}
