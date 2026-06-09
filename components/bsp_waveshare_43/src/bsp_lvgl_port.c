#include "bsp_waveshare_43.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_LVGL";

static SemaphoreHandle_t s_lvgl_mux  = NULL;
static TaskHandle_t      s_lvgl_task = NULL;

// ── Flush callback ────────────────────────────────────────
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1,
        color_map);
    lv_disp_flush_ready(drv);
}

// ── Tick callback ─────────────────────────────────────────
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

// ── Touch read callback ───────────────────────────────────
static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;

    uint16_t x, y;
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &cnt, 1);

    if (pressed && cnt > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Touch init ────────────────────────────────────────────
static void bsp_touch_init(void)
{
    // GPIO4 como saída — reset do GT911 via CH422G
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_4),
        .mode         = GPIO_MODE_OUTPUT,
        .intr_type    = GPIO_INTR_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_cfg);

    // Sequência de reset do GT911 via CH422G
    uint8_t buf;

    buf = 0x01;
    i2c_master_write_to_device(BSP_I2C_NUM, 0x24, &buf, 1,
        pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));

    buf = 0x2C;
    i2c_master_write_to_device(BSP_I2C_NUM, 0x38, &buf, 1,
        pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    esp_rom_delay_us(100 * 1000);

    gpio_set_level(GPIO_NUM_4, 0);
    esp_rom_delay_us(100 * 1000);

    buf = 0x2E;
    i2c_master_write_to_device(BSP_I2C_NUM, 0x38, &buf, 1,
        pdMS_TO_TICKS(BSP_I2C_TIMEOUT_MS));
    esp_rom_delay_us(200 * 1000);

    // Cria IO I2C para o GT911 sem setar scl_speed_hz
    // (driver legado usa frequência do i2c_param_config)
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr            = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,
        .lcd_param_bits      = 8,
        .flags = {
            .dc_low_on_data  = 0,
            .disable_control_phase = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)BSP_I2C_NUM, &tp_io_cfg, &tp_io));

    // Configuração do GT911
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,
        .y_max        = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };

    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp));

    // Registra no LVGL como input device
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type      = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb   = lvgl_touch_read_cb;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch GT911 inicializado");
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

    bsp_touch_init();

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