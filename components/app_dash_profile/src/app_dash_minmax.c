#include "app_dash_minmax.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS   "dash_mm"
#define NVS_KEY  "mm"

static const char *TAG = "APP_DASH_MM";

static app_dash_minmax_t s_mm;
static bool              s_have_data = false;   // true depois da 1a leitura real

static void save_locked(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, &s_mm, sizeof(s_mm));
    nvs_commit(h);
    nvs_close(h);
}

void app_dash_minmax_init(void)
{
    memset(&s_mm, 0, sizeof(s_mm));
    s_have_data = false;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_mm);
        if (nvs_get_blob(h, NVS_KEY, &s_mm, &sz) == ESP_OK && sz == sizeof(s_mm)) {
            s_have_data = true;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "app_dash_minmax pronto (dados salvos: %s)", s_have_data ? "sim" : "nao");
}

#define UPDATE_FIELD(field, value)                 \
    do {                                            \
        if ((value) < s_mm.field##_min) {           \
            s_mm.field##_min = (value);             \
            changed = true;                         \
        }                                           \
        if ((value) > s_mm.field##_max) {           \
            s_mm.field##_max = (value);              \
            changed = true;                         \
        }                                           \
    } while (0)

void app_dash_minmax_update(float rpm, float speed, float map_kpa, float tps,
                            float ect, float iat, float batt, float afr)
{
    if (!s_have_data) {
        s_mm.rpm_min   = s_mm.rpm_max   = rpm;
        s_mm.speed_min = s_mm.speed_max = speed;
        s_mm.map_min   = s_mm.map_max   = map_kpa;
        s_mm.tps_min   = s_mm.tps_max   = tps;
        s_mm.ect_min   = s_mm.ect_max   = ect;
        s_mm.iat_min   = s_mm.iat_max   = iat;
        s_mm.batt_min  = s_mm.batt_max  = batt;
        s_mm.afr_min   = s_mm.afr_max   = afr;
        s_have_data = true;
        save_locked();
        return;
    }

    bool changed = false;
    UPDATE_FIELD(rpm, rpm);
    UPDATE_FIELD(speed, speed);
    UPDATE_FIELD(map, map_kpa);
    UPDATE_FIELD(tps, tps);
    UPDATE_FIELD(ect, ect);
    UPDATE_FIELD(iat, iat);
    UPDATE_FIELD(batt, batt);
    UPDATE_FIELD(afr, afr);

    if (changed) {
        save_locked();
    }
}

#undef UPDATE_FIELD

void app_dash_minmax_get(app_dash_minmax_t *out)
{
    if (!out) return;
    *out = s_mm;
}

void app_dash_minmax_reset(void)
{
    s_have_data = false;
    memset(&s_mm, 0, sizeof(s_mm));
    save_locked();
    ESP_LOGI(TAG, "Minimo/maximo zerado");
}
