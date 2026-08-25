#include "app_dash_profile.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "dash_prof"

static const char *TAG = "APP_DASH_PROF";
static bool s_ready = false;

static bool key_for_index(uint32_t index, char *out, size_t sz)
{
    if (index >= APP_DASH_PROFILE_MAX) return false;
    snprintf(out, sz, "p%u", (unsigned)index);
    return true;
}

static void write_default_profile_locked(nvs_handle_t h)
{
    app_dash_profile_t def = {0};
    snprintf(def.name, sizeof(def.name), "Padrao");
    def.gauge_style = APP_DASH_GAUGE_DIGITAL;
    def.redline_rpm = 7000;
    def.color_theme = 0;

    char key[8];
    key_for_index(0, key, sizeof(key));
    nvs_set_blob(h, key, &def, sizeof(def));
    nvs_set_u8(h, "count", 1);
    nvs_set_i8(h, "active", 0);
    nvs_commit(h);
}

void app_dash_profile_init(void)
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
        ESP_LOGE(TAG, "nvs_open falhou");
        return;
    }

    uint8_t count = 0;
    if (nvs_get_u8(h, "count", &count) != ESP_OK || count == 0) {
        write_default_profile_locked(h);
    }

    nvs_close(h);
    s_ready = true;
    ESP_LOGI(TAG, "app_dash_profile pronto");
}

uint32_t app_dash_profile_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t count = 0;
    nvs_get_u8(h, "count", &count);
    nvs_close(h);
    return count;
}

bool app_dash_profile_get(uint32_t index, app_dash_profile_t *out)
{
    if (!out) return false;
    char key[8];
    if (!key_for_index(index, key, sizeof(key))) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, key, out, &sz);
    nvs_close(h);
    return (err == ESP_OK && sz == sizeof(*out));
}

bool app_dash_profile_save(uint32_t index, const app_dash_profile_t *profile)
{
    if (!profile) return false;
    char key[8];
    if (!key_for_index(index, key, sizeof(key))) return false;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    uint8_t count = 0;
    nvs_get_u8(h, "count", &count);

    esp_err_t err = nvs_set_blob(h, key, profile, sizeof(*profile));
    if (err == ESP_OK && index >= count) {
        nvs_set_u8(h, "count", (uint8_t)(index + 1));
    }
    if (err == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

bool app_dash_profile_delete(uint32_t index)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    uint8_t count = 0;
    nvs_get_u8(h, "count", &count);
    if (index >= count) { nvs_close(h); return false; }

    // Desloca os perfis seguintes uma posição pra trás, pra não deixar buraco.
    for (uint32_t i = index; i + 1 < count; i++) {
        app_dash_profile_t tmp;
        char key_from[8], key_to[8];
        key_for_index(i + 1, key_from, sizeof(key_from));
        key_for_index(i, key_to, sizeof(key_to));
        size_t sz = sizeof(tmp);
        if (nvs_get_blob(h, key_from, &tmp, &sz) == ESP_OK) {
            nvs_set_blob(h, key_to, &tmp, sizeof(tmp));
        }
    }
    char last_key[8];
    key_for_index(count - 1, last_key, sizeof(last_key));
    nvs_erase_key(h, last_key);
    nvs_set_u8(h, "count", (uint8_t)(count - 1));

    // Se o perfil ativo era o apagado (ou veio depois, e por isso deslocou
    // de índice), o mais simples e seguro é voltar pro primeiro perfil.
    int8_t active = -1;
    nvs_get_i8(h, "active", &active);
    if (active >= 0 && (uint32_t)active >= index) {
        nvs_set_i8(h, "active", 0);
    }

    // Nunca fica sem nenhum perfil — recria o padrão se a lista esvaziou.
    if (count - 1 == 0) {
        write_default_profile_locked(h);
    }

    nvs_commit(h);
    nvs_close(h);
    return true;
}

int32_t app_dash_profile_get_active_index(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    int8_t active = -1;
    nvs_get_i8(h, "active", &active);
    nvs_close(h);
    return active;
}

void app_dash_profile_set_active_index(uint32_t index)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i8(h, "active", (int8_t)index);
    nvs_commit(h);
    nvs_close(h);
}
