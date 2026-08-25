#include "bsp_waveshare_43.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/task.h"

static const char *TAG = "BSP_LVGL";

static bsp_touch_cb_t s_touch_cb = NULL;

static lv_indev_t  *s_touch_indev  = NULL;
static lv_coord_t   s_last_x       = 0;
static lv_coord_t   s_last_y       = 0;
static bool         s_last_pressed = false;
static bool         s_touch_dirty  = false;

// Diagnostic counters accessed by multiple tasks.
static volatile uint32_t s_render_count      = 0;
static volatile uint32_t s_flush_wait_ms     = 0;
static volatile uint32_t s_flush_slow_count  = 0;
static volatile uint32_t s_flush_tick_start  = 0;
static volatile bool     s_flush_in_progress = false;
static lv_obj_t          *s_fps_label         = NULL;
static uint32_t           s_fps_last_render   = 0;
static uint32_t           s_fps_last_tick     = 0;

void bsp_touch_register_cb(bsp_touch_cb_t cb)
{
    s_touch_cb = cb;
}

// LVGL event callbacks.

// Called after LVGL finishes rendering a frame.
static void on_render_ready_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_render_count++;
}

// Called before flush waits for VSYNC.
static void on_flush_wait_start_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_flush_in_progress = true;
    s_flush_tick_start  = (uint32_t)lv_tick_get();
}

// Called after VSYNC arrives and flush completes.
static void on_flush_wait_finish_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t dt = (uint32_t)lv_tick_get() - s_flush_tick_start;
    s_flush_wait_ms    = dt;
    s_flush_in_progress = false;
    if (dt > 60) {
        s_flush_slow_count++;
        ESP_LOGW(TAG, "[FLUSH] VSYNC slow: %lums (slow #%lu)",
                 (unsigned long)dt, (unsigned long)s_flush_slow_count);
    }
}

// Timer running inside the LVGL task.
static void lvgl_diag_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    static uint32_t last = 0;
    uint32_t now = s_render_count;
    // sizeof(StackType_t) = 4 no ESP32-S3.
    uint32_t stack_free = uxTaskGetStackHighWaterMark(NULL) * 4u;

    ESP_LOGI(TAG, "[DIAG-LVGL-TASK] tick=%-8lu | renders=%-4lu (+%lu/3s) | "
             "vsync_ms=%-4lu | lento=%lu | heap=%u | psram=%u | stack_free=%uB",
             (unsigned long)lv_tick_get(),
             (unsigned long)now,
             (unsigned long)(now - last),
             (unsigned long)s_flush_wait_ms,
             (unsigned long)s_flush_slow_count,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)stack_free);
    last = now;
}

// Medicao feita dentro da task do LVGL: nao disputa mutex nem conta frames
// parcialmente preparados. O label vive em lv_layer_top(), portanto continua
// visivel durante todas as trocas de tela.
static void fps_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    uint32_t now = (uint32_t)lv_tick_get();
    uint32_t elapsed = now - s_fps_last_tick;
    if (elapsed == 0 || !s_fps_label) return;

    uint32_t rendered = s_render_count;
    uint32_t fps = ((rendered - s_fps_last_render) * 1000u + elapsed / 2u) / elapsed;
    lv_label_set_text_fmt(s_fps_label, "FPS: %lu", (unsigned long)fps);
    s_fps_last_render = rendered;
    s_fps_last_tick = now;
}

static void create_fps_overlay(void)
{
    s_fps_label = lv_label_create(lv_layer_top());
    lv_label_set_text(s_fps_label, "FPS: --");
    lv_obj_set_style_text_font(s_fps_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_fps_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_fps_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_fps_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_fps_label, 5, 0);
    lv_obj_set_style_pad_ver(s_fps_label, 2, 0);
    lv_obj_set_style_radius(s_fps_label, 3, 0);
    lv_obj_align(s_fps_label, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_add_flag(s_fps_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    s_fps_last_render = s_render_count;
    s_fps_last_tick = (uint32_t)lv_tick_get();
    lv_timer_create(fps_timer_cb, 1000, NULL);
}

static void apply_fps_limit(lv_display_t *disp)
{
    lv_timer_t *refr_timer = lv_display_get_refr_timer(disp);
    if (refr_timer != NULL) {
        lv_timer_set_period(refr_timer, BSP_LVGL_REFR_PERIOD_MS);
    }

    lv_timer_t *anim_timer = lv_anim_get_timer();
    if (anim_timer != NULL) {
        lv_timer_set_period(anim_timer, BSP_LVGL_REFR_PERIOD_MS);
    }

    ESP_LOGI(TAG, "Cadencia alvo configurada em %d FPS (%dms por frame)",
             BSP_LVGL_TARGET_FPS, BSP_LVGL_REFR_PERIOD_MS);
}

// Independent monitor task.
static void monitor_task(void *arg)
{
    LV_UNUSED(arg);
    uint32_t last = 0;

    // Wait for startup before monitoring.
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));

        uint32_t now   = s_render_count;
        uint32_t delta = now - last;
        bool     stuck = s_flush_in_progress;

        ESP_LOGI(TAG, "[MON-TASK] tick=%-8lu | renders=%-4lu (+%lu/3s) | "
                 "flush_travado=%s | heap=%u | psram=%u",
                 (unsigned long)lv_tick_get(),
                 (unsigned long)now,
                 (unsigned long)delta,
                 stuck ? "SIM !!!" : "nao",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        if (delta == 0 && lv_tick_get() > 5000) {
            ESP_LOGE(TAG, "[MON-TASK] No render in 3s; LVGL task may be blocked");
        }
        if (stuck) {
            uint32_t wait = (uint32_t)lv_tick_get() - s_flush_tick_start;
            ESP_LOGE(TAG, "[MON-TASK] Flush wait active for %lums",
                     (unsigned long)wait);
        }

        last = now;
    }
}

// Touch init.
static esp_lcd_touch_handle_t bsp_touch_init(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_4),
        .mode         = GPIO_MODE_OUTPUT,
        .intr_type    = GPIO_INTR_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_cfg);

    ESP_ERROR_CHECK(bsp_touch_reset_set(false));
    esp_rom_delay_us(100 * 1000);

    gpio_set_level(GPIO_NUM_4, 0);
    esp_rom_delay_us(100 * 1000);

    ESP_ERROR_CHECK(bsp_touch_reset_set(true));
    esp_rom_delay_us(200 * 1000);

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

// ── Init principal ────────────────────────────────────────────
esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "Memoria antes do LVGL: heap=%u psram=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 1) Inicializa o port LVGL.
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority     = BSP_LVGL_TASK_PRIORITY,
        .task_stack        = BSP_LVGL_TASK_STACK_KB * 1024,
        .task_affinity     = BSP_LVGL_TASK_CORE,
        .task_max_sleep_ms = BSP_LVGL_TASK_MAX_DELAY_MS,
        .timer_period_ms   = BSP_LVGL_TICK_MS,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    ESP_LOGI(TAG, "lvgl_port_init OK | task_stack=%dKB core=%d prio=%d",
             BSP_LVGL_TASK_STACK_KB, BSP_LVGL_TASK_CORE, BSP_LVGL_TASK_PRIORITY);

    // 2) Registra o display RGB.
    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle  = panel,
        // Com avoid_tearing, o port usa os framebuffers RGB em PSRAM como
        // draw buffers. Isso evita alternancia de buffers com areas parciais
        // dessincronizadas ao pressionar/soltar objetos.
        .buffer_size   = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = false,
        .hres          = BSP_LCD_H_RES,
        .vres          = BSP_LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation      = { .swap_xy=false, .mirror_x=false, .mirror_y=false },
        .flags = {
            .buff_dma    = false,
            .buff_spiram = false,
            .swap_bytes  = false,
            .full_refresh = false,
            .direct_mode  = true,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            // Tentativa revertida: bb_mode=true trocava o sync de swap de
            // framebuffer de on_vsync (INICIO do frame) pra on_frame_buf_
            // complete (dispara so quando o bounce buffer termina de ler o
            // frame INTEIRO, ou seja, bem mais tarde). So atrasou o sinal
            // "pode desenhar no outro buffer" sem tocar na causa real —
            // piorou (passou a balancar em qualquer menu). Causa real:
            // ver esp_lcd_panel_rgb.c (IDF) lcd_rgb_panel_try_restart_
            // transmission() — se a ISR de refill do bounce buffer nao
            // acompanha o pixel clock (contencao de PSRAM sob carga de CPU),
            // o driver detecta desync e reinicia a DMA no meio do frame, o
            // que o proprio comentario do driver admite causar um shift
            // visivel de 1 frame. bb_mode nao influencia esse mecanismo.
            .bb_mode       = false,
            .avoid_tearing = true,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Falha ao registrar display");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Display registrado %p (%dx%d) | direct=true avoid_tearing=true fps=%d",
             disp,
             (int)lv_display_get_horizontal_resolution(disp),
             (int)lv_display_get_vertical_resolution(disp),
             BSP_LVGL_TARGET_FPS);

    // Registra callbacks de diagnostico dentro do mutex LVGL.
    if (bsp_lvgl_lock(-1)) {
        apply_fps_limit(disp);
        lv_display_add_event_cb(disp, on_render_ready_cb,
                                LV_EVENT_RENDER_READY,      NULL);
        lv_display_add_event_cb(disp, on_flush_wait_start_cb,
                                LV_EVENT_FLUSH_WAIT_START,  NULL);
        lv_display_add_event_cb(disp, on_flush_wait_finish_cb,
                                LV_EVENT_FLUSH_WAIT_FINISH, NULL);
        lv_timer_create(lvgl_diag_timer_cb, 3000, NULL);
        create_fps_overlay();
        bsp_lvgl_unlock();
    }

    // Task de monitoramento independente (core 0, prio baixa).
    xTaskCreatePinnedToCore(monitor_task, "lvgl_mon", 3072, NULL, 1, NULL, 0);

    // 3) Inicializa e registra o touch.
    esp_lcd_touch_handle_t tp = bsp_touch_init();

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = disp,
        .handle = tp,
    };
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "Falha ao registrar touch");
        return ESP_FAIL;
    }

    // GT911: ao segurar pressionado, a area de contato expande e o centroide
    // deriva ate ~20px. O LVGL default (10px) interpreta isso como scroll e
    // abandona o botao pressionado. Aumentar para 25px elimina falsos scrolls.
    lv_indev_set_scroll_limit(s_touch_indev, 25);

    ESP_LOGI(TAG, "LVGL %d.%d.%d pronto | heap=%u psram=%u",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}

// Handler: despacha callback de touch para a UI.
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
                ESP_LOGI(TAG, "Touch: x=%d y=%d %s",
                         (int)point.x, (int)point.y,
                         pressed ? "PRESS" : "RELEASE");
            }

            if (s_touch_dirty) {
                s_touch_cb(s_last_x, s_last_y, s_last_pressed);
                s_touch_dirty = false;
            }
            bsp_lvgl_unlock();
        } else {
            static uint32_t s_lock_timeouts = 0;
            s_lock_timeouts++;
            // Log a cada 5 timeouts para nao spammar.
            if (s_lock_timeouts <= 3 || s_lock_timeouts % 25 == 0) {
                ESP_LOGW(TAG, "[LOCK] Timeout #%lu (50ms) - task LVGL segurando mutex!",
                         (unsigned long)s_lock_timeouts);
            }
        }
    }
    vTaskDelay(2);
}

// Lock / Unlock.
bool bsp_lvgl_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
