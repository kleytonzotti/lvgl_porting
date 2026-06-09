#include "bsp_waveshare_43.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "BSP_LVGL";

static SemaphoreHandle_t s_lvgl_mux      = NULL;
static TaskHandle_t      s_lvgl_task     = NULL;

// ── Callback de flush ─────────────────────────────────────
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1,
        color_map);
    lv_disp_flush_ready(drv);
}

// ── Tick via esp_timer ────────────────────────────────────
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(BSP_LVGL_TICK_MS);
}

// ── Task LVGL ─────────────────────────────────────────────
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "Task LVGL iniciada");
    uint32_t delay_ms = BSP_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (xSemaphoreTakeRecursive(s_lvgl_mux, portMAX_DELAY) == pdTRUE) {
            delay_ms = lv_timer_handler();
            xSemaphoreGiveRecursive(s_lvgl_mux);
        }
        if (delay_ms > BSP_LVGL_TASK_MAX_DELAY_MS) delay_ms = BSP_LVGL_TASK_MAX_DELAY_MS;
        if (delay_ms < BSP_LVGL_TASK_MIN_DELAY_MS) delay_ms = BSP_LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ── Init principal ────────────────────────────────────────
esp_err_t bsp_lvgl_init(void)
{
    lv_init();

    // Tick timer
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, BSP_LVGL_TICK_MS * 1000));

    // Buffer de renderização em SRAM interna
    static lv_disp_draw_buf_t draw_buf;
    uint32_t buf_size = BSP_LCD_H_RES * BSP_LVGL_BUFFER_LINES;
    lv_color_t *buf = heap_caps_malloc(buf_size * sizeof(lv_color_t),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffer LVGL");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, buf_size);

    // Registra display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = BSP_LCD_H_RES;
    disp_drv.ver_res   = BSP_LCD_V_RES;
    disp_drv.flush_cb  = lvgl_flush_cb;
    disp_drv.draw_buf  = &draw_buf;
    disp_drv.user_data = bsp_lcd_get_panel_handle();
    lv_disp_drv_register(&disp_drv);

    // Mutex e task
    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    configASSERT(s_lvgl_mux);

    BaseType_t core = (BSP_LVGL_TASK_CORE < 0) ? tskNO_AFFINITY : BSP_LVGL_TASK_CORE;
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
        BSP_LVGL_TASK_STACK_KB * 1024,
        NULL, BSP_LVGL_TASK_PRIORITY,
        &s_lvgl_task, core);

    ESP_LOGI(TAG, "LVGL inicializado");
    return ESP_OK;
}

// ── Handler e lock público ────────────────────────────────
void bsp_lvgl_handler(void)
{
    // Não precisa mais fazer nada aqui — a task cuida disso
    // Mantido para compatibilidade com o main.c existente
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool bsp_lvgl_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mux, ticks) == pdTRUE;
}

void bsp_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mux);
}