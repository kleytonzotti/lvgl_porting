#include "bsp_waveshare_43.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_LVGL";

static bsp_touch_cb_t s_touch_cb = NULL;

// estado do touch — despachado fora do callback de leitura
static lv_indev_t  *s_touch_indev   = NULL;
static lv_coord_t   s_last_x        = 0;
static lv_coord_t   s_last_y        = 0;
static bool         s_last_pressed  = false;
static bool         s_touch_dirty   = false;

void bsp_touch_register_cb(bsp_touch_cb_t cb)
{
    s_touch_cb = cb;
}

// ── Touch init — retorna o handle esp_lcd_touch ──────────
static esp_lcd_touch_handle_t bsp_touch_init(void)
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
    // NOTA: comando 0x01→0x24 (config CH422G) omitido —
    // bsp_backlight_set() já o executa antes desta função.
    uint8_t buf;

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

    // IO I2C para o GT911 — sem scl_speed_hz (driver legado)
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr            = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 0,
        .lcd_cmd_bits        = 16,
        .lcd_param_bits      = 8,
        .flags = {
            .dc_low_on_data        = 0,
            .disable_control_phase = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)BSP_I2C_NUM, &tp_io_cfg, &tp_io));

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

    ESP_LOGI(TAG, "Touch GT911 inicializado");
    return tp;
}

// ── Init principal ────────────────────────────────────────
esp_err_t bsp_lvgl_init(void)
{
    // 1) Inicializa o port LVGL — cria task interna, mutex, etc.
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority   = BSP_LVGL_TASK_PRIORITY,
        .task_stack      = BSP_LVGL_TASK_STACK_KB * 1024,
        .task_affinity   = BSP_LVGL_TASK_CORE,
        .task_max_sleep_ms = BSP_LVGL_TASK_MAX_DELAY_MS,
        .timer_period_ms = BSP_LVGL_TICK_MS,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    ESP_LOGI(TAG, "lvgl_port_init OK");

    // 2) Registra o display RGB
    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();
    ESP_LOGI(TAG, "Panel handle = %p", panel);

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle     = panel,
        .buffer_size      = BSP_LCD_BOUNCE_BUFFER_PX,
        .double_buffer    = true,
        .hres             = BSP_LCD_H_RES,
        .vres             = BSP_LCD_V_RES,
        .monochrome       = false,
        .color_format     = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
            .swap_bytes  = false,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,
            .avoid_tearing = false,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Falha ao registrar display");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Display registrado: %p, hor_res=%d ver_res=%d",
             disp, (int)lv_display_get_horizontal_resolution(disp),
             (int)lv_display_get_vertical_resolution(disp));

    // 3) Inicializa e registra o touch
    esp_lcd_touch_handle_t tp = bsp_touch_init();

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp      = disp,
        .handle    = tp,
    };
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "Falha ao registrar touch");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL inicializado (LVGL %d.%d.%d)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return ESP_OK;
}

// ── Handler — despacha callback de touch para a UI ───────
void bsp_lvgl_handler(void)
{
    if (s_touch_indev && s_touch_cb) {
        if (bsp_lvgl_lock(50)) {
            lv_indev_state_t state = lv_indev_get_state(s_touch_indev);
            lv_point_t point;
            lv_indev_get_point(s_touch_indev, &point);

            bool pressed = (state == LV_INDEV_STATE_PRESSED);

            if (pressed != s_last_pressed ||
                point.x != s_last_x || point.y != s_last_y) {
                s_last_x       = point.x;
                s_last_y       = point.y;
                s_last_pressed = pressed;
                s_touch_dirty  = true;
            }

            if (s_touch_dirty) {
                s_touch_cb(s_last_x, s_last_y, s_last_pressed);
                s_touch_dirty = false;
            }
            bsp_lvgl_unlock();
        }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
}

// ── Lock / Unlock — delega para esp_lvgl_port ────────────
bool bsp_lvgl_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_lvgl_unlock(void)
{
    lvgl_port_unlock();
}