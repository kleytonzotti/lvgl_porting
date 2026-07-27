#include "app_dash_minmax.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS   "dash_mm"
#define NVS_KEY  "mm"

// Grava na NVS no máximo 1x a cada 3s, e sempre de dentro do esp_timer
// (task própria, NÃO a task do LVGL) — ver o comentário grande abaixo pra
// entender por que isso é obrigatório, não só uma otimização.
#define FLUSH_PERIOD_US (3 * 1000 * 1000)

static const char *TAG = "APP_DASH_MM";

static app_dash_minmax_t  s_mm;
static bool               s_have_data = false;   // true depois da 1a leitura real
static volatile bool      s_dirty     = false;    // tem recorde novo esperando gravar
static esp_timer_handle_t s_flush_timer;
static SemaphoreHandle_t  s_lock;

static void save_snapshot(const app_dash_minmax_t *data)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, data, sizeof(*data));
    nvs_commit(h);
    nvs_close(h);
}

static void save_now(void)
{
    app_dash_minmax_t snapshot;
    snapshot = s_mm;
    xSemaphoreGive(s_lock);
    save_snapshot(&snapshot);
}

// ─────────────────────────────────────────────────────
// IMPORTANTE — por que a gravação na NVS não pode acontecer dentro de
// app_dash_minmax_update(): essa função é chamada a cada 200ms de dentro
// de ui_screen_dashboard_update(), que roda na TASK DO LVGL (via lv_timer).
// nvs_commit() é uma escrita bloqueante na flash — pode levar vários
// milissegundos, às vezes mais se precisar apagar uma página. No modo
// demo, os valores simulados variam bastante logo no início (a simulação
// "explorando" toda a faixa), batendo recorde atrás de recorde — cada um
// disparando uma gravação bloqueante *dentro da renderização da tela*.
// É exatamente a mesma classe de bug que já tínhamos corrigido pro cartão
// SD (I/O bloqueante na task do LVGL trava/engasga a tela) — só que
// reintroduzida aqui sem perceber. Por isso a gravação de verdade só
// acontece aqui, disparada por um esp_timer (task própria do ESP-IDF,
// nunca a do LVGL), no máximo 1x a cada FLUSH_PERIOD_US.
// ─────────────────────────────────────────────────────
static void flush_timer_cb(void *arg)
{
    (void)arg;
    app_dash_minmax_t snapshot;
    bool save = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_dirty) {
        s_dirty = false;
        snapshot = s_mm;
        save = true;
    }
    xSemaphoreGive(s_lock);
    if (save) save_snapshot(&snapshot);
}

static void ensure_flush_timer(void)
{
    if (s_flush_timer) return;
    const esp_timer_create_args_t args = {
        .callback = flush_timer_cb,
        .name     = "dash_mm_flush",
    };
    esp_timer_create(&args, &s_flush_timer);
    esp_timer_start_periodic(s_flush_timer, FLUSH_PERIOD_US);
}

void app_dash_minmax_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
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
    xSemaphoreGive(s_lock);
    ensure_flush_timer();
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

// Rápido e sem I/O — só mexe em RAM. Seguro chamar a cada tick da tela.
void app_dash_minmax_update(float rpm, float speed, float map_kpa, float tps,
                            float ect, float iat, float batt, float afr)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
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
        s_dirty = true;
        xSemaphoreGive(s_lock);
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
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);
}

#undef UPDATE_FIELD

void app_dash_minmax_get(app_dash_minmax_t *out)
{
    if (!out || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_mm;
    xSemaphoreGive(s_lock);
}

void app_dash_minmax_reset(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_have_data = false;
    memset(&s_mm, 0, sizeof(s_mm));
    s_dirty = false;
    save_now();   // reset é ação explícita do usuário, não do timer de 200ms — ok ser direto
    ESP_LOGI(TAG, "Minimo/maximo zerado");
}
