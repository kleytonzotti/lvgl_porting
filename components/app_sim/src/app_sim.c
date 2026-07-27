#include "app_sim.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define APP_SIM_TASK_STACK     3072
#define APP_SIM_TASK_PRIORITY  3
#define APP_SIM_TICK_MS        33

static const char *TAG = "APP_SIM";

// Fases de um ciclo de condução simulado — só existem pra dar variação
// realista ao demo, não representam nada físico de verdade.
typedef enum {
    PHASE_IDLE = 0,
    PHASE_ACCEL,
    PHASE_CRUISE,
    PHASE_NEAR_REDLINE,
    PHASE_DECEL,
    PHASE_COUNT
} phase_t;

typedef struct {
    float rpm_target;
    float throttle_target;
    uint32_t min_ticks;
    uint32_t max_ticks;
} phase_def_t;

static SemaphoreHandle_t s_lock;
static bool              s_enabled     = false;
static uint16_t          s_redline_rpm = 7000;

static phase_t   s_phase       = PHASE_IDLE;
static uint32_t  s_phase_ticks = 0;
static uint32_t  s_phase_len   = 30;

static float s_rpm_f      = 850.0f;
static float s_throttle_f = 0.0f;
static float s_speed_f    = 0.0f;
static float s_speed_prev = 0.0f;
static float s_ect_f      = 20.0f;   // "frio" no boot, esquenta com o tempo
static uint32_t s_warmup_ticks = 0;

static app_sim_data_t s_data = {0};

// Ruído pequeno e determinístico o bastante (RNG de hardware do ESP32) só
// pra não ficar com número igual toda hora — não precisa de qualidade
// estatística nenhuma, é só cosmético.
static float noise(float amplitude)
{
    int32_t r = (int32_t)(esp_random() % 2001) - 1000;   // -1000..1000
    return ((float)r / 1000.0f) * amplitude;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static phase_def_t phase_def(phase_t ph)
{
    switch (ph) {
    case PHASE_IDLE:
        // Pausa curta apenas para a partida ficar perceptivel; o Demo nao
        // deve permanecer parado em 850 rpm por varios segundos.
        return (phase_def_t){ .rpm_target = 900, .throttle_target = 2,  .min_ticks = 8, .max_ticks = 15 };
    case PHASE_ACCEL:
        return (phase_def_t){ .rpm_target = (float)s_redline_rpm * 0.82f, .throttle_target = 88, .min_ticks = 120, .max_ticks = 210 };
    case PHASE_CRUISE:
        return (phase_def_t){ .rpm_target = (float)s_redline_rpm * 0.52f, .throttle_target = 28, .min_ticks = 90, .max_ticks = 150 };
    case PHASE_NEAR_REDLINE:
        return (phase_def_t){ .rpm_target = (float)s_redline_rpm * 0.96f, .throttle_target = 96, .min_ticks = 30, .max_ticks = 60 };
    case PHASE_DECEL:
    default:
        return (phase_def_t){ .rpm_target = 1200, .throttle_target = 0, .min_ticks = 75, .max_ticks = 120 };
    }
}

static void advance_phase_if_needed(void)
{
    s_phase_ticks++;
    if (s_phase_ticks < s_phase_len) return;

    s_phase = (phase_t)((s_phase + 1) % PHASE_COUNT);
    s_phase_ticks = 0;
    phase_def_t def = phase_def(s_phase);
    uint32_t span = (def.max_ticks > def.min_ticks) ? (def.max_ticks - def.min_ticks) : 1;
    s_phase_len = def.min_ticks + (esp_random() % span);
}

static void sim_tick(void)
{
    advance_phase_if_needed();
    phase_def_t def = phase_def(s_phase);

    // Suavização exponencial simples (filtro passa-baixa) — dá a sensação
    // de "motor acelerando/desacelerando" em vez de saltos instantâneos.
    s_rpm_f      += (def.rpm_target - s_rpm_f) * 0.12f;
    s_throttle_f += (def.throttle_target - s_throttle_f) * 0.18f;

    float speed_target = clampf(s_rpm_f / (float)s_redline_rpm * 220.0f, 0.0f, 220.0f);
    s_speed_prev = s_speed_f;
    s_speed_f   += (speed_target - s_speed_f) * 0.06f;   // reage mais devagar que o RPM (troca de marcha)

    // Aquecimento do motor: sobe devagar até ~90C e fica lá com ruído pequeno.
    s_warmup_ticks++;
    float ect_target = 20.0f + clampf((float)s_warmup_ticks / 6.0f, 0.0f, 70.0f);
    s_ect_f += (ect_target - s_ect_f) * 0.05f;

    float map_kpa = clampf(30.0f + s_throttle_f * 0.65f, 20.0f, 100.0f);
    float iat_c   = 26.0f + (s_throttle_f / 100.0f) * 6.0f + noise(2.0f);
    float batt_v  = 13.9f + noise(0.15f);

    // Enriquecimento em aceleração/perto do corte, um pouco mais pobre
    // em desaceleração — só pra dar variação visível no medidor de lambda.
    float lambda_base = (s_phase == PHASE_ACCEL || s_phase == PHASE_NEAR_REDLINE) ? 0.90f :
                         (s_phase == PHASE_DECEL) ? 1.06f : 1.00f;
    float lambda = lambda_base + noise(0.02f);

    // dt = 0.1s (APP_SIM_TICK_MS) — conversão grosseira de km/h -> g.
    float dv_ms  = (s_speed_f - s_speed_prev) * (1000.0f / 3600.0f);
    float dt_s = (float)APP_SIM_TICK_MS / 1000.0f;
    float accel_g = clampf((dv_ms / dt_s) / 9.81f, -1.2f, 1.2f);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_enabled) {
        s_data.rpm          = (uint16_t)clampf(s_rpm_f, 0.0f, 65535.0f);
        s_data.speed_kph    = (uint8_t)clampf(s_speed_f, 0.0f, 255.0f);
        s_data.throttle_pct = (uint8_t)clampf(s_throttle_f, 0.0f, 100.0f);
        s_data.map_kpa      = (uint8_t)map_kpa;
        s_data.tps_pct      = s_data.throttle_pct;
        s_data.ect_c        = (int8_t)clampf(s_ect_f, -40.0f, 127.0f);
        s_data.iat_c        = (int8_t)clampf(iat_c, -40.0f, 127.0f);
        s_data.batt_v       = batt_v;
        s_data.lambda       = lambda;
        s_data.accel_g      = accel_g;
    }
    xSemaphoreGive(s_lock);
}

static void sim_task(void *arg)
{
    (void)arg;
    for (;;) {
        sim_tick();
        vTaskDelay(pdMS_TO_TICKS(APP_SIM_TICK_MS));
    }
}

void app_sim_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(&s_data, 0, sizeof(s_data));

    static TaskHandle_t task = NULL;
    if (!task) {
        xTaskCreate(sim_task, "app_sim", APP_SIM_TASK_STACK, NULL, APP_SIM_TASK_PRIORITY, &task);
    }
    ESP_LOGI(TAG, "app_sim pronto (desligado por padrao)");
}

void app_sim_set_enabled(bool enable)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (enable && !s_enabled) {
        // Todo inicio de Demo parte de um estado visivel e previsivel. Antes,
        // reativar o modo continuava o ciclo anterior sem indicacao na tela.
        s_phase = PHASE_IDLE;
        s_phase_ticks = 0;
        s_phase_len = 8;
        s_rpm_f = 850.0f;
        s_throttle_f = 0.0f;
        s_speed_f = 0.0f;
        s_speed_prev = 0.0f;
        s_ect_f = 20.0f;
        s_warmup_ticks = 0;
        memset(&s_data, 0, sizeof(s_data));
        s_data.rpm = 850;
        s_data.ect_c = 20;
        s_data.iat_c = 26;
        s_data.batt_v = 13.9f;
        s_data.lambda = 1.0f;
    }
    s_enabled = enable;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Demo %s", enable ? "ativado" : "desativado");
}

bool app_sim_is_enabled(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}

void app_sim_set_redline(uint16_t redline_rpm)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (redline_rpm > 0) {
        s_redline_rpm = redline_rpm;
    }
    xSemaphoreGive(s_lock);
}

uint16_t app_sim_get_redline(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t redline = s_redline_rpm;
    xSemaphoreGive(s_lock);
    return redline;
}

void app_sim_get_data(app_sim_data_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}
