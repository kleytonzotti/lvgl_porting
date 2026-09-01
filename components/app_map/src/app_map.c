#include "app_map.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG   = "APP_MAP";
#define NVS_NS            "map_prof"
#define NVS_KEY           "set"

static bool           s_ready = false;
static app_map_set_t  s_current;

// ─────────────────────────────────────────────────────
// Curva base / persistencia local (NVS do painel) — ver comentario grande
// em app_map.h sobre por que isto e so um CACHE, nao a fonte de verdade.
// ─────────────────────────────────────────────────────

void app_map_reset_default(app_map_set_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    static const uint16_t rpm[APP_MAP_RPM_BINS]   = {800, 1500, 2200, 3000, 4000, 5000, 6000, 7000};
    static const uint16_t load[APP_MAP_LOAD_BINS] = {20, 35, 50, 65, 80, 100};
    memcpy(out->rpm_bins, rpm, sizeof(rpm));
    memcpy(out->load_kpa_bins, load, sizeof(load));

    // Curva "segura pra editar" — NAO e calibracao real de motor nenhum, e
    // so um ponto de partida com formato plausivel (mais combustivel e
    // menos avanco em carga alta; mais avanco em RPM alto e carga baixa),
    // pra nao comecar a editar de uma tabela zerada. Recalibrar sempre no
    // motor real antes de rodar serio.
    //
    // GARANTIA DE SUAVIDADE ("sem quebra"): cada formula abaixo e LINEAR
    // em load_frac/rpm_frac e os breakpoints (rpm[]/load[]) sao
    // crescentes — entao a superficie gerada e continua, sem saltos entre
    // celulas vizinhas. O unico jeito de introduzir uma "quebra" de
    // verdade seria a formula bater no clamp MIN/MAX no meio da grade
    // (a celula fica achatada no limite, mudando a inclinacao ali) — as
    // constantes abaixo foram escolhidas pra NUNCA saturar dentro da
    // grade real (rpm 800-7000, carga 20-100kPa); ver o teste
    // "reset_default gera curva sem quebra" em test_app_map.c, que trava
    // se um ajuste futuro nas constantes voltar a saturar o clamp.
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        float load_frac = (float)load[r] / 100.0f;
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            float rpm_frac = (float)rpm[c] / 7000.0f;

            float inj10 = 20.0f + load_frac * 80.0f + rpm_frac * 40.0f;
            if (inj10 < (float)APP_MAP_INJ_MIN_TENTHS) inj10 = (float)APP_MAP_INJ_MIN_TENTHS;
            if (inj10 > (float)APP_MAP_INJ_MAX_TENTHS) inj10 = (float)APP_MAP_INJ_MAX_TENTHS;
            out->injecao[r][c] = (int16_t)inj10;

            float ign10 = 100.0f + rpm_frac * 250.0f - load_frac * 100.0f;
            if (ign10 < (float)APP_MAP_IGN_MIN_TENTHS) ign10 = (float)APP_MAP_IGN_MIN_TENTHS;
            if (ign10 > (float)APP_MAP_IGN_MAX_TENTHS) ign10 = (float)APP_MAP_IGN_MAX_TENTHS;
            out->ignicao[r][c] = (int16_t)ign10;

            // Alvo de lambda: perto de estequiometrico/levemente pobre em
            // carga leve (economia), enriquecendo com carga e RPM (margem
            // termica) — de novo, so um formato plausivel de partida.
            float sonda100 = 105.0f - load_frac * 20.0f - rpm_frac * 5.0f;
            if (sonda100 < (float)APP_MAP_SONDA_MIN_X100) sonda100 = (float)APP_MAP_SONDA_MIN_X100;
            if (sonda100 > (float)APP_MAP_SONDA_MAX_X100) sonda100 = (float)APP_MAP_SONDA_MAX_X100;
            out->sonda[r][c] = (int16_t)sonda100;
        }
    }
}

void app_map_init(void)
{
    if (s_ready) return;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open falhou — mapa padrao so em RAM (nao persiste)");
        app_map_reset_default(&s_current);
        s_ready = true;
        return;
    }

    size_t len = sizeof(s_current);
    if (nvs_get_blob(h, NVS_KEY, &s_current, &len) != ESP_OK || len != sizeof(s_current)) {
        app_map_reset_default(&s_current);
        nvs_set_blob(h, NVS_KEY, &s_current, sizeof(s_current));
        nvs_commit(h);
    }
    nvs_close(h);

    s_ready = true;
}

void app_map_get(app_map_set_t *out)
{
    if (!out) return;
    if (!s_ready) app_map_init();
    *out = s_current;
}

bool app_map_save_local(const app_map_set_t *in)
{
    if (!in) return false;
    if (!s_ready) app_map_init();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    esp_err_t err = nvs_set_blob(h, NVS_KEY, in, sizeof(*in));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) s_current = *in;
    return err == ESP_OK;
}

// ─────────────────────────────────────────────────────
// Protocolo BLE baseado em UDS (ISO 14229-1) — ver o comentario grande em
// app_map.h pro porque e a sequencia completa.
// ─────────────────────────────────────────────────────

static uint16_t table_address(app_map_table_id_t table_id)
{
    switch (table_id) {
    case APP_MAP_TABLE_INJECAO: return APP_MAP_ADDR_INJECAO;
    case APP_MAP_TABLE_IGNICAO: return APP_MAP_ADDR_IGNICAO;
    case APP_MAP_TABLE_SONDA:   return APP_MAP_ADDR_SONDA;
    default:                    return 0xFFFF;   // invalido — a ECU deve responder requestOutOfRange
    }
}

uint16_t app_map_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void app_map_serialize_table(const app_map_set_t *set, app_map_table_id_t table_id, uint8_t *out)
{
    if (!set || !out) return;

    size_t off = 0;
    for (int i = 0; i < APP_MAP_RPM_BINS; i++) {
        out[off++] = (uint8_t)(set->rpm_bins[i] & 0xFF);
        out[off++] = (uint8_t)((set->rpm_bins[i] >> 8) & 0xFF);
    }
    for (int i = 0; i < APP_MAP_LOAD_BINS; i++) {
        out[off++] = (uint8_t)(set->load_kpa_bins[i] & 0xFF);
        out[off++] = (uint8_t)((set->load_kpa_bins[i] >> 8) & 0xFF);
    }

    const int16_t (*cells)[APP_MAP_RPM_BINS];
    switch (table_id) {
    case APP_MAP_TABLE_INJECAO: cells = set->injecao; break;
    case APP_MAP_TABLE_IGNICAO: cells = set->ignicao; break;
    case APP_MAP_TABLE_SONDA:   cells = set->sonda;   break;
    default:                    return;   // table_id invalido — nao serializa nada
    }
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            uint16_t v = (uint16_t)cells[r][c];
            out[off++] = (uint8_t)(v & 0xFF);
            out[off++] = (uint8_t)((v >> 8) & 0xFF);
        }
    }
}

uint8_t app_map_build_session_control(uint8_t session, uint8_t *out)
{
    out[0] = APP_MAP_SID_DIAG_SESSION_CONTROL;
    out[1] = session;
    return 2;
}

uint8_t app_map_build_security_seed_request(uint8_t *out)
{
    out[0] = APP_MAP_SID_SECURITY_ACCESS;
    out[1] = APP_MAP_SECURITY_REQUEST_SEED;
    return 2;
}

uint16_t app_map_security_compute_key(uint16_t seed)
{
    // Transformacao simples DE DEMONSTRACAO (ver aviso grande em
    // app_map.h) — rotaciona 3 bits e faz XOR com uma constante fixa. Nao
    // e criptografia real; o objetivo e so recusar escrita de um app BLE
    // generico por acidente/curiosidade, nao resistir a um atacante que
    // capturou o trafego.
    uint16_t rotated = (uint16_t)((seed << 3) | (seed >> 13));
    return (uint16_t)(rotated ^ 0xA5A5);
}

uint8_t app_map_build_security_send_key(uint16_t seed, uint8_t *out)
{
    uint16_t key = app_map_security_compute_key(seed);
    out[0] = APP_MAP_SID_SECURITY_ACCESS;
    out[1] = APP_MAP_SECURITY_SEND_KEY;
    out[2] = (uint8_t)(key & 0xFF);
    out[3] = (uint8_t)((key >> 8) & 0xFF);
    return 4;
}

uint8_t app_map_build_request_download(app_map_table_id_t table_id, uint8_t *out)
{
    uint16_t addr = table_address(table_id);
    out[0] = APP_MAP_SID_REQUEST_DOWNLOAD;
    out[1] = 0x00;  // dataFormatIdentifier: sem compressao/criptografia
    out[2] = 0x22;  // addressAndLengthFormatIdentifier: 2 bytes endereco + 2 bytes tamanho
    out[3] = (uint8_t)(addr & 0xFF);
    out[4] = (uint8_t)((addr >> 8) & 0xFF);
    out[5] = (uint8_t)(APP_MAP_SERIALIZED_LEN & 0xFF);
    out[6] = (uint8_t)((APP_MAP_SERIALIZED_LEN >> 8) & 0xFF);
    return 7;
}

void app_map_build_transfer_data_pdus(const app_map_set_t *set, app_map_table_id_t table_id,
                                       uint8_t max_block_len,
                                       app_map_pdu_cb_t on_pdu, void *ctx)
{
    if (!set || !on_pdu || max_block_len == 0) return;

    uint8_t buf[APP_MAP_SERIALIZED_LEN];
    app_map_serialize_table(set, table_id, buf);

    uint8_t pdu[APP_MAP_PDU_MAX_LEN];
    uint8_t bsc    = 0x01;   // blockSequenceCounter comeca em 1, por padrao do UDS
    size_t  offset = 0;
    while (offset < APP_MAP_SERIALIZED_LEN) {
        size_t  remaining = APP_MAP_SERIALIZED_LEN - offset;
        uint8_t len       = (uint8_t)(remaining > max_block_len ? max_block_len : remaining);
        if ((size_t)(2 + len) > sizeof(pdu)) len = (uint8_t)(sizeof(pdu) - 2);

        pdu[0] = APP_MAP_SID_TRANSFER_DATA;
        pdu[1] = bsc;
        memcpy(&pdu[2], &buf[offset], len);
        on_pdu(ctx, pdu, (uint8_t)(2 + len));

        offset += len;
        bsc++;   // uint8_t: da a volta sozinho 0xFF -> 0x00, igual ao padrao real
    }
}

uint8_t app_map_build_transfer_exit(const app_map_set_t *set, app_map_table_id_t table_id, uint8_t *out)
{
    uint8_t buf[APP_MAP_SERIALIZED_LEN];
    app_map_serialize_table(set, table_id, buf);
    uint16_t crc = app_map_crc16(buf, APP_MAP_SERIALIZED_LEN);

    out[0] = APP_MAP_SID_REQUEST_TRANSFER_EXIT;
    out[1] = (uint8_t)(crc & 0xFF);
    out[2] = (uint8_t)((crc >> 8) & 0xFF);
    return 3;
}

bool app_map_parse_response(const uint8_t *pdu, uint8_t len, app_map_response_t *out)
{
    if (!pdu || !out || len < 2) return false;

    if (pdu[0] == APP_MAP_SID_NEGATIVE_RESPONSE) {
        if (len < 3) return false;
        out->positive   = false;
        out->service_id = pdu[1];
        out->nrc        = pdu[2];
        return true;
    }

    out->positive   = true;
    out->service_id = (uint8_t)(pdu[0] - APP_MAP_POSITIVE_RESPONSE_OFFSET);
    out->nrc        = 0;
    return true;
}

// DEBUG TEMPORARIO (ver app_map_set_debug_sniffer em app_map.h) — se
// ninguem registrar, isto fica sempre NULL e o custo e um "if" por
// chamada de app_map_send_to_ecu().
static app_map_pdu_cb_t s_debug_sniffer     = NULL;
static void            *s_debug_sniffer_ctx = NULL;

void app_map_set_debug_sniffer(app_map_pdu_cb_t cb, void *ctx)
{
    s_debug_sniffer = cb;
    s_debug_sniffer_ctx = ctx;
}

esp_err_t app_map_send_to_ecu(const app_map_set_t *set, app_map_table_id_t table_id)
{
    // DEBUG TEMPORARIO: com um sniffer registrado, monta e entrega a ele a
    // sequencia completa de PDUs que um GATT client de verdade mandaria —
    // nao muda o que a funcao retorna. O seed usado pra ilustrar o
    // send_key e um valor fixo (0x1234): sem ECU de verdade nao ha seed
    // real vindo de lugar nenhum, isto e so pra mostrar o formato do PDU.
    if (s_debug_sniffer) {
        uint8_t pdu[APP_MAP_PDU_MAX_LEN];
        uint8_t len;

        len = app_map_build_session_control(APP_MAP_SESSION_PROGRAMMING, pdu);
        s_debug_sniffer(s_debug_sniffer_ctx, pdu, len);

        len = app_map_build_security_seed_request(pdu);
        s_debug_sniffer(s_debug_sniffer_ctx, pdu, len);

        len = app_map_build_security_send_key(0x1234, pdu);
        s_debug_sniffer(s_debug_sniffer_ctx, pdu, len);

        len = app_map_build_request_download(table_id, pdu);
        s_debug_sniffer(s_debug_sniffer_ctx, pdu, len);

        app_map_build_transfer_data_pdus(set, table_id, 16, s_debug_sniffer, s_debug_sniffer_ctx);

        len = app_map_build_transfer_exit(set, table_id, pdu);
        s_debug_sniffer(s_debug_sniffer_ctx, pdu, len);
    }

    // Falta o GATT client de ESCRITA em app_ble.c — mesma pendencia do
    // subscribe de telemetria do app_ecu (ROADMAP.md §4). Os PDUs ja podem
    // ser montados e decodificados de verdade (funcoes acima, testadas em
    // Unity); so falta o transporte real pra rodar a sequencia completa:
    //   1. app_map_build_session_control(APP_MAP_SESSION_PROGRAMMING, ...)
    //   2. app_map_build_security_seed_request(...) -> ler seed da resposta
    //      -> app_map_build_security_send_key(seed, ...)
    //   3. app_map_build_request_download(table_id, ...) -> ler
    //      maxNumberOfBlockLength da resposta
    //   4. app_map_build_transfer_data_pdus(..., max_block_len, ...) em loop
    //   5. app_map_build_transfer_exit(...)
    // Cada passo espera a resposta (app_map_parse_response) antes do
    // proximo — se vier negativa, aborta e reporta o NRC.
    ESP_LOGW(TAG, "Envio de mapa por BLE ainda nao implementado (falta GATT client de escrita)");
    return ESP_ERR_NOT_SUPPORTED;
}

// ─────────────────────────────────────────────────────
// Leitura (ECU -> Painel) — RequestUpload. Ver comentario grande em
// app_map.h pro porque a ECU e sempre a fonte de verdade.
// ─────────────────────────────────────────────────────

uint8_t app_map_build_request_upload(app_map_table_id_t table_id, uint8_t *out)
{
    uint16_t addr = table_address(table_id);
    out[0] = APP_MAP_SID_REQUEST_UPLOAD;
    out[1] = 0x00;  // dataFormatIdentifier: sem compressao/criptografia
    out[2] = 0x22;  // addressAndLengthFormatIdentifier: 2 bytes endereco + 2 bytes tamanho
    out[3] = (uint8_t)(addr & 0xFF);
    out[4] = (uint8_t)((addr >> 8) & 0xFF);
    out[5] = (uint8_t)(APP_MAP_SERIALIZED_LEN & 0xFF);
    out[6] = (uint8_t)((APP_MAP_SERIALIZED_LEN >> 8) & 0xFF);
    return 7;
}

uint8_t app_map_build_transfer_data_request(uint8_t bsc, uint8_t *out)
{
    out[0] = APP_MAP_SID_TRANSFER_DATA;
    out[1] = bsc;
    return 2;
}

bool app_map_parse_transfer_data_response(const uint8_t *pdu, uint8_t len, uint8_t *out_bsc,
                                            const uint8_t **out_data, uint8_t *out_data_len)
{
    if (!pdu || len < 2) return false;
    if (pdu[0] != (uint8_t)(APP_MAP_SID_TRANSFER_DATA + APP_MAP_POSITIVE_RESPONSE_OFFSET)) return false;

    if (out_bsc)      *out_bsc = pdu[1];
    if (out_data)     *out_data = &pdu[2];
    if (out_data_len) *out_data_len = (uint8_t)(len - 2);
    return true;
}

uint8_t app_map_build_transfer_exit_read(uint8_t *out)
{
    out[0] = APP_MAP_SID_REQUEST_TRANSFER_EXIT;
    return 1;
}

void app_map_deserialize_table(const uint8_t *buf, app_map_table_id_t table_id, app_map_set_t *out)
{
    if (!buf || !out) return;

    size_t off = 0;
    for (int i = 0; i < APP_MAP_RPM_BINS; i++) {
        out->rpm_bins[i] = (uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8);
        off += 2;
    }
    for (int i = 0; i < APP_MAP_LOAD_BINS; i++) {
        out->load_kpa_bins[i] = (uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8);
        off += 2;
    }

    int16_t (*cells)[APP_MAP_RPM_BINS];
    switch (table_id) {
    case APP_MAP_TABLE_INJECAO: cells = out->injecao; break;
    case APP_MAP_TABLE_IGNICAO: cells = out->ignicao; break;
    case APP_MAP_TABLE_SONDA:   cells = out->sonda;   break;
    default:                    return;  // table_id invalido — nao decodifica celulas
    }
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            uint16_t v = (uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8);
            cells[r][c] = (int16_t)v;
            off += 2;
        }
    }
}

esp_err_t app_map_read_from_ecu(app_map_table_id_t table_id, app_map_set_t *out)
{
    (void)table_id;
    (void)out;
    // Falta o GATT client em app_ble.c — mesma pendencia do
    // app_map_send_to_ecu(). Os PDUs de leitura ja podem ser montados e
    // decodificados de verdade (funcoes acima, testadas em Unity); so
    // falta o transporte real pra rodar a sequencia:
    //   1. app_map_build_request_upload(table_id, ...)
    //   2. loop: app_map_build_transfer_data_request(bsc, ...) -> escreve
    //      -> le a resposta (notify) -> app_map_parse_transfer_data_response(...)
    //      -> acumula o dado num buffer de APP_MAP_SERIALIZED_LEN bytes
    //   3. app_map_build_transfer_exit_read(...) -> le o CRC16 que a ECU
    //      devolve, recalcula com app_map_crc16() sobre o buffer
    //      reassemblado e SO aceita se bater
    //   4. app_map_deserialize_table(buf, table_id, out)
    // Enquanto isso nao existir, o chamador deve tratar o retorno
    // ESP_ERR_NOT_SUPPORTED honestamente — nunca assumir 'out' valido.
    ESP_LOGW(TAG, "Leitura de mapa por BLE ainda nao implementada (falta GATT client)");
    return ESP_ERR_NOT_SUPPORTED;
}
