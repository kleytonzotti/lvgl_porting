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
// Protocolo BLE (framing) — ver o comentario grande em app_map.h.
// ─────────────────────────────────────────────────────

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

void app_map_build_packets(const app_map_set_t *set, app_map_table_id_t table_id,
                            app_map_packet_cb_t on_packet, void *ctx)
{
    if (!set || !on_packet) return;

    uint8_t buf[APP_MAP_SERIALIZED_LEN];
    app_map_serialize_table(set, table_id, buf);

    uint8_t pkt[APP_MAP_PACKET_MAX_LEN];
    uint8_t chk;

    // MAP_BEGIN
    pkt[0] = APP_MAP_FRAME_MARKER_CMD;
    pkt[1] = APP_MAP_PROTO_VERSION;
    pkt[2] = APP_MAP_MSG_BEGIN;
    pkt[3] = (uint8_t)table_id;
    pkt[4] = APP_MAP_RPM_BINS;
    pkt[5] = APP_MAP_LOAD_BINS;
    pkt[6] = (uint8_t)(APP_MAP_SERIALIZED_LEN & 0xFF);
    pkt[7] = (uint8_t)((APP_MAP_SERIALIZED_LEN >> 8) & 0xFF);
    chk = 0;
    for (int i = 0; i < 8; i++) chk ^= pkt[i];
    pkt[8] = chk;
    on_packet(ctx, pkt, 9);

    // MAP_CHUNK — fatiado em ate APP_MAP_CHUNK_MAX_DATA bytes de dado por pacote.
    uint16_t seq    = 0;
    size_t   offset = 0;
    while (offset < APP_MAP_SERIALIZED_LEN) {
        size_t remaining = APP_MAP_SERIALIZED_LEN - offset;
        uint8_t len = (uint8_t)(remaining > APP_MAP_CHUNK_MAX_DATA ? APP_MAP_CHUNK_MAX_DATA : remaining);

        pkt[0] = APP_MAP_FRAME_MARKER_CMD;
        pkt[1] = APP_MAP_PROTO_VERSION;
        pkt[2] = APP_MAP_MSG_CHUNK;
        pkt[3] = (uint8_t)(seq & 0xFF);
        pkt[4] = (uint8_t)((seq >> 8) & 0xFF);
        pkt[5] = len;
        memcpy(&pkt[6], &buf[offset], len);
        chk = 0;
        for (int i = 0; i < 6 + len; i++) chk ^= pkt[i];
        pkt[6 + len] = chk;
        on_packet(ctx, pkt, (uint8_t)(6 + len + 1));

        offset += len;
        seq++;
    }

    // MAP_END
    uint16_t crc = app_map_crc16(buf, APP_MAP_SERIALIZED_LEN);
    pkt[0] = APP_MAP_FRAME_MARKER_CMD;
    pkt[1] = APP_MAP_PROTO_VERSION;
    pkt[2] = APP_MAP_MSG_END;
    pkt[3] = (uint8_t)table_id;
    pkt[4] = (uint8_t)(crc & 0xFF);
    pkt[5] = (uint8_t)((crc >> 8) & 0xFF);
    chk = 0;
    for (int i = 0; i < 6; i++) chk ^= pkt[i];
    pkt[6] = chk;
    on_packet(ctx, pkt, 7);
}

esp_err_t app_map_send_to_ecu(const app_map_set_t *set, app_map_table_id_t table_id)
{
    (void)set;
    (void)table_id;
    // Falta o GATT client de ESCRITA em app_ble.c — mesma pendencia do
    // subscribe de telemetria do app_ecu (ROADMAP.md §4). Os pacotes ja
    // podem ser montados de verdade (app_map_build_packets), so falta o
    // transporte: escrever cada um na characteristic e esperar o status
    // via notify (app_map_status_t).
    ESP_LOGW(TAG, "Envio de mapa por BLE ainda nao implementado (falta GATT client de escrita)");
    return ESP_ERR_NOT_SUPPORTED;
}
