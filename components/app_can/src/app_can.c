#include "app_can.h"

#include <stdio.h>
#include <string.h>

#include "bsp_waveshare_43.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/twai.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define APP_CAN_RX_QUEUE_LEN       64
#define APP_CAN_UI_QUEUE_LEN       80
#define APP_CAN_TASK_STACK         4096
#define APP_CAN_TASK_PRIORITY      6
#define APP_CAN_SD_MOUNT_POINT     "/sdcard"
#define APP_CAN_LOG_FILE           "/sdcard/can_sniffer.log"
#define APP_CAN_LOG_FLUSH_EVERY    16

typedef struct {
    char text[APP_CAN_SNIFF_LINE_MAX];
} app_can_line_t;

static const char *TAG = "APP_CAN";

static QueueHandle_t s_line_queue = NULL;
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static FILE *s_log_file = NULL;
static sdmmc_card_t *s_sd_card = NULL;
static bool s_spi_bus_ready = false;
static bool s_sd_mounted = false;
static bool s_driver_ready = false;
static bool s_driver_started = false;
static bool s_sniffer_enabled = false;
static uint32_t s_flush_counter = 0;
static app_can_status_t s_status = {0};

static void set_error_locked(const char *msg)
{
    s_status.state = APP_CAN_STATE_ERROR;
    snprintf(s_status.last_error, sizeof(s_status.last_error), "%s", msg);
}

static void set_error(const char *msg)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_error_locked(msg);
        xSemaphoreGive(s_lock);
    }
}

static void format_frame_line(const twai_message_t *msg, char *out, size_t out_size)
{
    size_t pos = 0;
    uint32_t id = msg->identifier;

    pos += snprintf(out + pos, out_size - pos, "%08lu %s ID:%0*lX DLC:%u",
                    (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                    msg->extd ? "EXT" : "STD",
                    msg->extd ? 8 : 3,
                    (unsigned long)id,
                    (unsigned int)msg->data_length_code);

    if (msg->rtr) {
        snprintf(out + pos, out_size - pos, " RTR");
        return;
    }

    pos += snprintf(out + pos, out_size - pos, " DATA:");
    for (uint8_t i = 0; i < msg->data_length_code && i < TWAI_FRAME_MAX_DLC; i++) {
        pos += snprintf(out + pos, out_size - pos, " %02X", msg->data[i]);
        if (pos >= out_size) {
            break;
        }
    }
}

static void queue_line(const char *line)
{
    app_can_line_t item = {0};
    snprintf(item.text, sizeof(item.text), "%s", line);

    if (xQueueSend(s_line_queue, &item, 0) != pdTRUE) {
        app_can_line_t dropped;
        (void)xQueueReceive(s_line_queue, &dropped, 0);
        if (xQueueSend(s_line_queue, &item, 0) != pdTRUE) {
            if (s_lock) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.dropped_lines++;
                xSemaphoreGive(s_lock);
            }
            return;
        }
        if (s_lock) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.dropped_lines++;
            xSemaphoreGive(s_lock);
        }
    }
}

static esp_err_t mount_sd_card(void)
{
    if (s_sd_mounted) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_sdcard_select(true), TAG, "Failed to select TF card");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_SD_MOSI,
        .miso_io_num = BSP_SD_MISO,
        .sclk_io_num = BSP_SD_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    if (!s_spi_bus_ready) {
        esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_spi_bus_ready = true;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(APP_CAN_SD_MOUNT_POINT,
                                            &host,
                                            &slot_cfg,
                                            &mount_cfg,
                                            &s_sd_card);
    if (err == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "TF card mounted at %s", APP_CAN_SD_MOUNT_POINT);
    }
    return err;
}

static esp_err_t open_log_file(void)
{
    if (s_log_file) {
        return ESP_OK;
    }

    s_log_file = fopen(APP_CAN_LOG_FILE, "a");
    if (!s_log_file) {
        return ESP_FAIL;
    }

    fprintf(s_log_file, "\n# CAN sniffer start\n");
    fflush(s_log_file);
    s_flush_counter = 0;
    return ESP_OK;
}

static void write_log_line(const char *line)
{
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_log_file) {
        fputs(line, s_log_file);
        fputc('\n', s_log_file);
        s_flush_counter++;
        if (s_flush_counter >= APP_CAN_LOG_FLUSH_EVERY) {
            fflush(s_log_file);
            s_flush_counter = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

static esp_err_t ensure_twai_driver(void)
{
    if (s_driver_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_can_set_selected(true), TAG, "Failed to select CAN mode");

    if (!s_driver_ready) {
        twai_general_config_t g_config =
            TWAI_GENERAL_CONFIG_DEFAULT(BSP_CAN_TX, BSP_CAN_RX, TWAI_MODE_LISTEN_ONLY);
        g_config.rx_queue_len = APP_CAN_RX_QUEUE_LEN;
        g_config.tx_queue_len = 0;
        g_config.alerts_enabled = TWAI_ALERT_RX_DATA |
                                  TWAI_ALERT_RX_QUEUE_FULL |
                                  TWAI_ALERT_BUS_ERROR |
                                  TWAI_ALERT_ERR_PASS |
                                  TWAI_ALERT_BUS_OFF;

        twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_driver_ready = true;
    }

    esp_err_t err = twai_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_driver_started = true;
    return ESP_OK;
}

static void close_log_file(void)
{
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_log_file) {
        fprintf(s_log_file, "# CAN sniffer stop\n");
        fflush(s_log_file);
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_status.log_open = false;
    xSemaphoreGive(s_lock);
}

static void can_sniffer_task(void *arg)
{
    (void)arg;

    for (;;) {
        if (!s_sniffer_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        twai_message_t msg = {0};
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.rx_errors++;
            xSemaphoreGive(s_lock);
            continue;
        }

        char line[APP_CAN_SNIFF_LINE_MAX];
        format_frame_line(&msg, line, sizeof(line));
        queue_line(line);
        write_log_line(line);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.frames++;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t app_can_init(void)
{
    if (!s_line_queue) {
        s_line_queue = xQueueCreate(APP_CAN_UI_QUEUE_LEN, sizeof(app_can_line_t));
        if (!s_line_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = APP_CAN_STATE_STOPPED;
    s_status.driver_ready = false;
    s_status.sd_mounted = false;
    s_status.log_open = false;
    s_status.last_error[0] = '\0';
    xSemaphoreGive(s_lock);

    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(can_sniffer_task,
                                                "can_sniffer",
                                                APP_CAN_TASK_STACK,
                                                NULL,
                                                APP_CAN_TASK_PRIORITY,
                                                &s_task,
                                                0);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "CAN service ready");
    return ESP_OK;
}

esp_err_t app_can_sniffer_start(void)
{
    if (!s_line_queue || !s_lock) {
        ESP_RETURN_ON_ERROR(app_can_init(), TAG, "CAN init failed");
    }

    esp_err_t err = ensure_twai_driver();
    if (err != ESP_OK) {
        set_error("TWAI start failed");
        return err;
    }

    bool sd_ok = (mount_sd_card() == ESP_OK);
    bool log_ok = false;
    if (sd_ok) {
        log_ok = (open_log_file() == ESP_OK);
    }

    xQueueReset(s_line_queue);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = APP_CAN_STATE_RUNNING;
    s_status.driver_ready = s_driver_started;
    s_status.sd_mounted = sd_ok;
    s_status.log_open = log_ok;
    s_status.last_error[0] = '\0';
    if (!sd_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "TF mount failed");
    } else if (!log_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "Log open failed");
    }
    s_sniffer_enabled = true;
    xSemaphoreGive(s_lock);

    queue_line(log_ok ? "SNIF START - SD LOG OK" : "SNIF START - SD LOG OFF");
    return ESP_OK;
}

void app_can_sniffer_stop(void)
{
    s_sniffer_enabled = false;
    close_log_file();

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.state = APP_CAN_STATE_STOPPED;
        s_status.log_open = false;
        xSemaphoreGive(s_lock);
    }

    queue_line("SNIF STOP");
}

bool app_can_sniffer_poll_line(char *out, size_t out_size)
{
    if (!out || out_size == 0 || !s_line_queue) {
        return false;
    }

    app_can_line_t item;
    if (xQueueReceive(s_line_queue, &item, 0) != pdTRUE) {
        return false;
    }

    snprintf(out, out_size, "%s", item.text);
    return true;
}

void app_can_sniffer_get_status(app_can_status_t *out_status)
{
    if (!out_status) {
        return;
    }

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        *out_status = s_status;
        xSemaphoreGive(s_lock);
    } else {
        memset(out_status, 0, sizeof(*out_status));
    }
}

const char *app_can_sniffer_log_path(void)
{
    return APP_CAN_LOG_FILE;
}
