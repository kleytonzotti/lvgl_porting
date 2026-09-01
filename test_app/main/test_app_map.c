#include <string.h>
#include "unity.h"
#include "app_map.h"

// Testa só a lógica pura (framing UDS/CRC/curva padrão) — sem tocar NVS de
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

// "Sem quebra" — pedido explicito (2026-09-01): a curva padrao nao pode
// ter descontinuidade. As formulas de app_map_reset_default() sao
// lineares em load_frac/rpm_frac (continuas por construcao); a UNICA
// forma de aparecer uma "quebra" seria a formula bater no clamp MIN/MAX
// no meio da grade real (a celula fica achatada no limite, mudando a
// inclinacao ali). Este teste trava se isso acontecer — nenhuma celula
// das 3 tabelas pode ficar EXATAMENTE no limite (sinal de que saturou).
TEST_CASE("app_map: reset_default gera curva sem quebra (nunca satura o clamp na grade real)", "[app_map]")
{
    app_map_set_t set;
    app_map_reset_default(&set);

    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            TEST_ASSERT_TRUE(set.injecao[r][c] > APP_MAP_INJ_MIN_TENTHS);
            TEST_ASSERT_TRUE(set.injecao[r][c] < APP_MAP_INJ_MAX_TENTHS);
            TEST_ASSERT_TRUE(set.ignicao[r][c] > APP_MAP_IGN_MIN_TENTHS);
            TEST_ASSERT_TRUE(set.ignicao[r][c] < APP_MAP_IGN_MAX_TENTHS);
            TEST_ASSERT_TRUE(set.sonda[r][c] > APP_MAP_SONDA_MIN_X100);
            TEST_ASSERT_TRUE(set.sonda[r][c] < APP_MAP_SONDA_MAX_X100);
        }
    }
}

// ─────────────────────────────────────────────────────
// Protocolo UDS (ISO 14229-1) — ver app_map.h pro porque desta escolha.
// ─────────────────────────────────────────────────────

TEST_CASE("app_map: session control monta [0x10][session]", "[app_map]")
{
    uint8_t pdu[8];
    uint8_t len = app_map_build_session_control(APP_MAP_SESSION_PROGRAMMING, pdu);
    TEST_ASSERT_EQUAL_UINT8(2, len);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_DIAG_SESSION_CONTROL, pdu[0]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SESSION_PROGRAMMING, pdu[1]);
}

TEST_CASE("app_map: security seed request monta [0x27][0x01]", "[app_map]")
{
    uint8_t pdu[8];
    uint8_t len = app_map_build_security_seed_request(pdu);
    TEST_ASSERT_EQUAL_UINT8(2, len);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_SECURITY_ACCESS, pdu[0]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SECURITY_REQUEST_SEED, pdu[1]);
}

// A key so precisa ser DETERMINISTICA e REVERSIVEL do seed (nao e
// criptografia real, ver aviso em app_map.h) — o teste recalcula com a
// MESMA formula (rotaciona 3 bits + XOR 0xA5A5) pra pegar regressao se
// alguem mudar um lado sem lembrar do outro (painel/ECU tem que combinar).
TEST_CASE("app_map: security key e deterministica e a PDU de send_key embute ela little-endian", "[app_map]")
{
    uint16_t seed = 0x1234;
    uint16_t rotated  = (uint16_t)((seed << 3) | (seed >> 13));
    uint16_t expected_key = (uint16_t)(rotated ^ 0xA5A5);

    TEST_ASSERT_EQUAL_UINT16(expected_key, app_map_security_compute_key(seed));

    uint8_t pdu[8];
    uint8_t len = app_map_build_security_send_key(seed, pdu);
    TEST_ASSERT_EQUAL_UINT8(4, len);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_SECURITY_ACCESS, pdu[0]);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SECURITY_SEND_KEY, pdu[1]);
    uint16_t key_in_pdu = (uint16_t)pdu[2] | ((uint16_t)pdu[3] << 8);
    TEST_ASSERT_EQUAL_UINT16(expected_key, key_in_pdu);
}

TEST_CASE("app_map: request_download declara endereco logico e tamanho certos por tabela", "[app_map]")
{
    static const uint16_t expected_addr[APP_MAP_TABLE_COUNT] = {
        [APP_MAP_TABLE_INJECAO] = APP_MAP_ADDR_INJECAO,
        [APP_MAP_TABLE_IGNICAO] = APP_MAP_ADDR_IGNICAO,
        [APP_MAP_TABLE_SONDA]   = APP_MAP_ADDR_SONDA,
    };

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        uint8_t pdu[8];
        uint8_t len = app_map_build_request_download((app_map_table_id_t)t, pdu);
        TEST_ASSERT_EQUAL_UINT8(7, len);
        TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_REQUEST_DOWNLOAD, pdu[0]);
        TEST_ASSERT_EQUAL_UINT8(0x00, pdu[1]);   // dataFormatIdentifier: sem compressao
        TEST_ASSERT_EQUAL_UINT8(0x22, pdu[2]);   // addrLenFmt: 2 bytes endereco + 2 bytes tamanho
        uint16_t addr = (uint16_t)pdu[3] | ((uint16_t)pdu[4] << 8);
        uint16_t size = (uint16_t)pdu[5] | ((uint16_t)pdu[6] << 8);
        TEST_ASSERT_EQUAL_UINT16(expected_addr[t], addr);
        TEST_ASSERT_EQUAL_UINT16(APP_MAP_SERIALIZED_LEN, size);
    }
}

// Captura os PDUs TransferData na ordem em que app_map_build_transfer_data_pdus os gera.
typedef struct {
    uint8_t pdus[16][APP_MAP_PDU_MAX_LEN];
    uint8_t lens[16];
    int     count;
} pdu_capture_t;

static void capture_pdu_cb(void *ctx, const uint8_t *pdu, uint8_t len)
{
    pdu_capture_t *cap = (pdu_capture_t *)ctx;
    TEST_ASSERT_TRUE(cap->count < 16);
    memcpy(cap->pdus[cap->count], pdu, len);
    cap->lens[cap->count] = len;
    cap->count++;
}

TEST_CASE("app_map: transfer_data fatia com blockSequenceCounter crescente e reassembla igual ao serialize", "[app_map]")
{
    app_map_set_t set;
    app_map_reset_default(&set);

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        app_map_table_id_t tid = (app_map_table_id_t)t;

        pdu_capture_t cap = {0};
        app_map_build_transfer_data_pdus(&set, tid, 16, capture_pdu_cb, &cap);

        TEST_ASSERT_GREATER_OR_EQUAL(1, cap.count);

        uint8_t reassembled[APP_MAP_SERIALIZED_LEN] = {0};
        size_t  off = 0;
        for (int i = 0; i < cap.count; i++) {
            const uint8_t *pdu = cap.pdus[i];
            uint8_t        len = cap.lens[i];

            TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_TRANSFER_DATA, pdu[0]);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(i + 1), pdu[1]);   // BSC comeca em 1, incrementa

            uint8_t data_len = (uint8_t)(len - 2);
            TEST_ASSERT_TRUE(off + data_len <= sizeof(reassembled));
            memcpy(&reassembled[off], &pdu[2], data_len);
            off += data_len;
        }
        TEST_ASSERT_EQUAL_UINT32(APP_MAP_SERIALIZED_LEN, off);

        uint8_t expected[APP_MAP_SERIALIZED_LEN];
        app_map_serialize_table(&set, tid, expected);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, reassembled, APP_MAP_SERIALIZED_LEN);
    }
}

TEST_CASE("app_map: transfer_exit embute o crc16 do buffer serializado inteiro", "[app_map]")
{
    app_map_set_t set;
    app_map_reset_default(&set);

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        app_map_table_id_t tid = (app_map_table_id_t)t;

        uint8_t serialized[APP_MAP_SERIALIZED_LEN];
        app_map_serialize_table(&set, tid, serialized);
        uint16_t expected_crc = app_map_crc16(serialized, APP_MAP_SERIALIZED_LEN);

        uint8_t pdu[8];
        uint8_t len = app_map_build_transfer_exit(&set, tid, pdu);
        TEST_ASSERT_EQUAL_UINT8(3, len);
        TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_REQUEST_TRANSFER_EXIT, pdu[0]);
        uint16_t crc_in_pdu = (uint16_t)pdu[1] | ((uint16_t)pdu[2] << 8);
        TEST_ASSERT_EQUAL_UINT16(expected_crc, crc_in_pdu);
    }
}

TEST_CASE("app_map: parse_response decodifica positiva e negativa (com NRC) corretamente", "[app_map]")
{
    app_map_response_t resp;

    // Positiva: 0x74 = 0x34 (RequestDownload) + 0x40.
    uint8_t positive[] = {0x74, 0x20, 0x10};
    TEST_ASSERT_TRUE(app_map_parse_response(positive, sizeof(positive), &resp));
    TEST_ASSERT_TRUE(resp.positive);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_REQUEST_DOWNLOAD, resp.service_id);

    // Negativa: motor girando ao tentar entrar em sessao de programacao.
    uint8_t negative[] = {APP_MAP_SID_NEGATIVE_RESPONSE, APP_MAP_SID_DIAG_SESSION_CONTROL,
                           APP_MAP_NRC_CONDITIONS_NOT_CORRECT};
    TEST_ASSERT_TRUE(app_map_parse_response(negative, sizeof(negative), &resp));
    TEST_ASSERT_FALSE(resp.positive);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_DIAG_SESSION_CONTROL, resp.service_id);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_NRC_CONDITIONS_NOT_CORRECT, resp.nrc);

    // PDU curto demais — deve recusar em vez de ler lixo.
    uint8_t too_short[] = {APP_MAP_SID_NEGATIVE_RESPONSE, APP_MAP_SID_DIAG_SESSION_CONTROL};
    TEST_ASSERT_FALSE(app_map_parse_response(too_short, sizeof(too_short), &resp));
}

// ─────────────────────────────────────────────────────
// Leitura (RequestUpload, 0x35) — par de leitura do RequestDownload.
// Pedido explicito (2026-09-01): a ECU e a fonte de verdade, o painel
// precisa poder LER o mapa dela, nao so escrever.
// ─────────────────────────────────────────────────────

TEST_CASE("app_map: request_upload declara endereco logico e tamanho certos por tabela", "[app_map]")
{
    static const uint16_t expected_addr[APP_MAP_TABLE_COUNT] = {
        [APP_MAP_TABLE_INJECAO] = APP_MAP_ADDR_INJECAO,
        [APP_MAP_TABLE_IGNICAO] = APP_MAP_ADDR_IGNICAO,
        [APP_MAP_TABLE_SONDA]   = APP_MAP_ADDR_SONDA,
    };

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        uint8_t pdu[8];
        uint8_t len = app_map_build_request_upload((app_map_table_id_t)t, pdu);
        TEST_ASSERT_EQUAL_UINT8(7, len);
        TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_REQUEST_UPLOAD, pdu[0]);
        TEST_ASSERT_EQUAL_UINT8(0x00, pdu[1]);
        TEST_ASSERT_EQUAL_UINT8(0x22, pdu[2]);
        uint16_t addr = (uint16_t)pdu[3] | ((uint16_t)pdu[4] << 8);
        uint16_t size = (uint16_t)pdu[5] | ((uint16_t)pdu[6] << 8);
        TEST_ASSERT_EQUAL_UINT16(expected_addr[t], addr);
        TEST_ASSERT_EQUAL_UINT16(APP_MAP_SERIALIZED_LEN, size);
    }
}

TEST_CASE("app_map: transfer_data_request monta [0x36][bsc] sem dado", "[app_map]")
{
    uint8_t pdu[4];
    uint8_t len = app_map_build_transfer_data_request(0x05, pdu);
    TEST_ASSERT_EQUAL_UINT8(2, len);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_TRANSFER_DATA, pdu[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, pdu[1]);
}

TEST_CASE("app_map: parse_transfer_data_response extrai bsc e dado, rejeita PDU que nao e 0x76", "[app_map]")
{
    uint8_t ok_pdu[] = { (uint8_t)(APP_MAP_SID_TRANSFER_DATA + APP_MAP_POSITIVE_RESPONSE_OFFSET),
                         0x03, 0xAA, 0xBB, 0xCC };
    uint8_t bsc;
    const uint8_t *data;
    uint8_t data_len;
    TEST_ASSERT_TRUE(app_map_parse_transfer_data_response(ok_pdu, sizeof(ok_pdu), &bsc, &data, &data_len));
    TEST_ASSERT_EQUAL_UINT8(0x03, bsc);
    TEST_ASSERT_EQUAL_UINT8(3, data_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, data[2]);

    // Negativa (0x7F) ou qualquer coisa que nao seja 0x76 nao e uma
    // resposta de dado valida — a funcao deve recusar, nao interpretar.
    uint8_t neg_pdu[] = { APP_MAP_SID_NEGATIVE_RESPONSE, APP_MAP_SID_TRANSFER_DATA, APP_MAP_NRC_REQUEST_SEQUENCE_ERROR };
    TEST_ASSERT_FALSE(app_map_parse_transfer_data_response(neg_pdu, sizeof(neg_pdu), &bsc, &data, &data_len));
}

TEST_CASE("app_map: transfer_exit_read monta [0x37] sem parametro", "[app_map]")
{
    uint8_t pdu[4];
    uint8_t len = app_map_build_transfer_exit_read(pdu);
    TEST_ASSERT_EQUAL_UINT8(1, len);
    TEST_ASSERT_EQUAL_UINT8(APP_MAP_SID_REQUEST_TRANSFER_EXIT, pdu[0]);
}

// O caminho que mais importa: serialize -> deserialize tem que devolver
// EXATAMENTE os mesmos eixos e celulas — e o que garante que reconstruir
// um mapa a partir dos TransferData recebidos da ECU da o mesmo resultado
// de quem tinha os dados originais.
TEST_CASE("app_map: deserialize_table e o inverso exato de serialize_table", "[app_map]")
{
    app_map_set_t original;
    app_map_reset_default(&original);

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        app_map_table_id_t tid = (app_map_table_id_t)t;

        uint8_t buf[APP_MAP_SERIALIZED_LEN];
        app_map_serialize_table(&original, tid, buf);

        app_map_set_t roundtrip = {0};
        app_map_deserialize_table(buf, tid, &roundtrip);

        TEST_ASSERT_EQUAL_UINT16_ARRAY(original.rpm_bins, roundtrip.rpm_bins, APP_MAP_RPM_BINS);
        TEST_ASSERT_EQUAL_UINT16_ARRAY(original.load_kpa_bins, roundtrip.load_kpa_bins, APP_MAP_LOAD_BINS);

        for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
            for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
                int16_t orig_v, rt_v;
                switch (tid) {
                case APP_MAP_TABLE_INJECAO: orig_v = original.injecao[r][c]; rt_v = roundtrip.injecao[r][c]; break;
                case APP_MAP_TABLE_IGNICAO: orig_v = original.ignicao[r][c]; rt_v = roundtrip.ignicao[r][c]; break;
                default:                    orig_v = original.sonda[r][c];   rt_v = roundtrip.sonda[r][c];   break;
                }
                TEST_ASSERT_EQUAL_INT16(orig_v, rt_v);
            }
        }
    }
}
