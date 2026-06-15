#include "app_can.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp_waveshare_43.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/twai.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define APP_CAN_RX_QUEUE_LEN    64
#define APP_CAN_TASK_STACK      4096
#define APP_CAN_TASK_PRIORITY   6
#define APP_CAN_SD_MOUNT        "/sdcard"
#define APP_CAN_FLUSH_EVERY     32   // flush CSV to SD every N frames

static const char *TAG = "APP_CAN";

// --- Per-ID table (protected by s_lock) ---
static app_can_id_entry_t s_id_table[APP_CAN_MAX_IDS];
static uint32_t           s_id_count = 0;

// --- State ---
static SemaphoreHandle_t s_lock           = NULL;
static TaskHandle_t      s_task           = NULL;
static FILE             *s_log_file       = NULL;
static sdmmc_card_t     *s_sd_card        = NULL;
static bool              s_spi_bus_ready  = false;
static bool              s_sd_mounted     = false;
static bool              s_driver_ready   = false;
static bool              s_driver_started = false;
static volatile bool     s_running        = false;
static uint32_t          s_flush_counter  = 0;
static char              s_log_path[40]   = "";
static app_can_status_t  s_status         = {0};

// --- Software filter ---
static volatile uint32_t s_filter_min = 0;
static volatile uint32_t s_filter_max = 0x1FFFFFFF;

// ─────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────

static app_can_id_entry_t *find_or_add_id_locked(uint32_t id, bool extd)
{
    for (uint32_t i = 0; i < s_id_count; i++) {
        if (s_id_table[i].id == id && s_id_table[i].extd == extd) {
            return &s_id_table[i];
        }
    }
    if (s_id_count >= APP_CAN_MAX_IDS) {
        return NULL;
    }
    app_can_id_entry_t *e = &s_id_table[s_id_count++];
    memset(e, 0, sizeof(*e));
    e->id   = id;
    e->extd = extd;
    s_status.unique_ids = s_id_count;
    return e;
}

// CSV: ms,type,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7
static void format_csv_line(const twai_message_t *msg, uint32_t ms,
                            char *out, size_t sz)
{
    int pos = snprintf(out, sz, "%lu,%s,%lX,%u",
                       (unsigned long)ms,
                       msg->extd ? "EXT" : "STD",
                       (unsigned long)msg->identifier,
                       (unsigned)msg->data_length_code);

    for (int i = 0; i < 8; i++) {
        if (pos >= (int)sz - 2) break;
        if (i < msg->data_length_code && i < TWAI_FRAME_MAX_DLC) {
            pos += snprintf(out + pos, sz - (size_t)pos, ",%02X", msg->data[i]);
        } else {
            out[pos++] = ',';
        }
    }
}

static esp_err_t mount_sd_card(void)
{
    if (s_sd_mounted) return ESP_OK;

    ESP_RETURN_ON_ERROR(bsp_sdcard_select(true), TAG, "SD select failed");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = BSP_SD_MOSI,
        .miso_io_num     = BSP_SD_MISO,
        .sclk_io_num     = BSP_SD_SCLK,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    if (!s_spi_bus_ready) {
        esp_err_t e = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
        s_spi_bus_ready = true;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(APP_CAN_SD_MOUNT, &host,
                                             &slot_cfg, &mount_cfg, &s_sd_card);
    if (err == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD mounted at %s", APP_CAN_SD_MOUNT);
    }
    return err;
}

static void find_new_log_path(char *out, size_t sz)
{
    for (int n = 1; n <= 999; n++) {
        snprintf(out, sz, APP_CAN_SD_MOUNT "/can_%03d.csv", n);
        FILE *f = fopen(out, "r");
        if (!f) return;   // file does not exist → use this slot
        fclose(f);
    }
    snprintf(out, sz, APP_CAN_SD_MOUNT "/can_999.csv");
}

static esp_err_t open_log_file_locked(void)
{
    if (s_log_file) return ESP_OK;

    find_new_log_path(s_log_path, sizeof(s_log_path));
    s_log_file = fopen(s_log_path, "w");
    if (!s_log_file) {
        s_log_path[0] = '\0';
        return ESP_FAIL;
    }

    // CSV header
    fputs("ms,type,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7\n", s_log_file);
    fflush(s_log_file);
    s_flush_counter = 0;
    snprintf(s_status.log_path, sizeof(s_status.log_path), "%s", s_log_path);
    s_status.log_open = true;
    return ESP_OK;
}

static void close_log_file_locked(void)
{
    if (s_log_file) {
        fflush(s_log_file);
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_status.log_open = false;
}

// Write one CSV line — protected by s_lock so close can't race.
static void write_csv_locked(const char *line)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_log_file) {
        fputs(line, s_log_file);
        fputc('\n', s_log_file);
        if (++s_flush_counter >= APP_CAN_FLUSH_EVERY) {
            fflush(s_log_file);
            s_flush_counter = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

static esp_err_t ensure_twai_driver(void)
{
    if (s_driver_started) return ESP_OK;

    ESP_RETURN_ON_ERROR(bsp_can_set_selected(true), TAG, "CAN select failed");

    if (!s_driver_ready) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
            BSP_CAN_TX, BSP_CAN_RX, TWAI_MODE_LISTEN_ONLY);
        g.rx_queue_len   = APP_CAN_RX_QUEUE_LEN;
        g.tx_queue_len   = 0;
        g.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                           TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;

        twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t e = twai_driver_install(&g, &t, &f);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
        s_driver_ready = true;
    }

    esp_err_t e = twai_start();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_driver_started = true;
    return ESP_OK;
}

// ─────────────────────────────────────────────────────
// CAN capture task — core 0, priority 6
// ─────────────────────────────────────────────────────

static void can_sniffer_task(void *arg)
{
    (void)arg;
    char csv[APP_CAN_CSV_LINE_MAX];

    for (;;) {
        if (!s_running) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        twai_message_t msg = {0};
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.rx_errors++;
            xSemaphoreGive(s_lock);
            continue;
        }

        // Software filter
        if (msg.identifier < s_filter_min || msg.identifier > s_filter_max) {
            continue;
        }

        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // Update per-ID table and frame counter
        xSemaphoreTake(s_lock, portMAX_DELAY);
        app_can_id_entry_t *e = find_or_add_id_locked(msg.identifier, msg.extd);
        if (e) {
            if (e->count > 0 && e->last_ms > 0) {
                uint32_t interval = now_ms - e->last_ms;
                if (interval >= 1 && interval < 60000) {
                    uint32_t hz10 = 10000u / interval;
                    e->hz_x10 = (hz10 > 0xFFFF) ? 0xFFFF : (uint16_t)hz10;
                }
            }
            e->last_ms = now_ms;
            e->dlc = msg.data_length_code;
            memcpy(e->data, msg.data, msg.data_length_code);
            e->count++;
        }
        s_status.frames++;
        xSemaphoreGive(s_lock);

        // Write CSV (mutex protects against close() race)
        format_csv_line(&msg, now_ms, csv, sizeof(csv));
        write_csv_locked(csv);
    }
}

// ─────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────

esp_err_t app_can_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state       = APP_CAN_STATE_STOPPED;
    s_status.driver_ready = false;
    s_status.sd_mounted  = false;
    s_status.log_open    = false;
    s_status.last_error[0] = '\0';
    xSemaphoreGive(s_lock);

    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(can_sniffer_task, "can_sniff",
                                                APP_CAN_TASK_STACK, NULL,
                                                APP_CAN_TASK_PRIORITY, &s_task, 0);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "CAN service ready");
    return ESP_OK;
}

esp_err_t app_can_sniffer_start(void)
{
    if (!s_lock) {
        ESP_RETURN_ON_ERROR(app_can_init(), TAG, "CAN init failed");
    }

    esp_err_t err = ensure_twai_driver();
    if (err != ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.state = APP_CAN_STATE_ERROR;
        snprintf(s_status.last_error, sizeof(s_status.last_error), "TWAI start failed");
        xSemaphoreGive(s_lock);
        return err;
    }

    bool sd_ok  = (mount_sd_card() == ESP_OK);
    bool log_ok = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.sd_mounted   = sd_ok;
    s_status.driver_ready = s_driver_started;
    if (sd_ok) {
        log_ok = (open_log_file_locked() == ESP_OK);
    }
    if (!sd_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "SD mount failed");
    } else if (!log_ok) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "Log open failed");
    } else {
        s_status.last_error[0] = '\0';
    }
    s_status.state  = APP_CAN_STATE_RUNNING;
    s_status.frames = 0;
    s_running = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sniffer started — log=%s", s_log_path[0] ? s_log_path : "none");
    return ESP_OK;
}

void app_can_sniffer_stop(void)
{
    s_running = false;
    // Wait for the CAN task to finish any in-progress write (receive timeout = 50ms)
    vTaskDelay(pdMS_TO_TICKS(100));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_log_file_locked();
    s_status.state = APP_CAN_STATE_STOPPED;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sniffer stopped — %lu frames", (unsigned long)s_status.frames);
}

uint32_t app_can_get_id_table(app_can_id_entry_t *out, uint32_t max_entries)
{
    if (!out || !s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t count = (s_id_count < max_entries) ? s_id_count : max_entries;
    memcpy(out, s_id_table, count * sizeof(app_can_id_entry_t));
    xSemaphoreGive(s_lock);
    return count;
}

void app_can_clear_id_table(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_id_table, 0, sizeof(s_id_table));
    s_id_count = 0;
    s_status.unique_ids = 0;
    s_status.frames     = 0;
    xSemaphoreGive(s_lock);
}

void app_can_set_filter(uint32_t id_min, uint32_t id_max)
{
    s_filter_min = id_min;
    s_filter_max = id_max;
}

void app_can_get_filter(uint32_t *id_min_out, uint32_t *id_max_out)
{
    if (id_min_out) *id_min_out = s_filter_min;
    if (id_max_out) *id_max_out = s_filter_max;
}

void app_can_sniffer_get_status(app_can_status_t *out)
{
    if (!out) return;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        *out = s_status;
        xSemaphoreGive(s_lock);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

const char *app_can_sniffer_log_path(void)
{
    return s_log_path[0] ? s_log_path : "---";
}

bool app_can_sd_is_mounted(void)
{
    return s_sd_mounted;
}

int app_can_sd_list_csv(app_can_sd_file_t *out, uint32_t max_files)
{
    if (!s_sd_mounted || !out) return -1;

    DIR *dir = opendir(APP_CAN_SD_MOUNT);
    if (!dir) return -1;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && (uint32_t)count < max_files) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        // match .csv extension (case-insensitive)
        const char *ext = name + len - 4;
        if (!(ext[0]=='.' && (ext[1]=='c'||ext[1]=='C') &&
              (ext[2]=='s'||ext[2]=='S') && (ext[3]=='v'||ext[3]=='V'))) continue;

        snprintf(out[count].name, sizeof(out[count].name), "%s", name);

        // Get size
        char full[64];
        snprintf(full, sizeof(full), APP_CAN_SD_MOUNT "/%s", name);
        struct stat st;
        out[count].size_kb = (stat(full, &st) == 0) ? (uint32_t)(st.st_size / 1024) : 0;
        count++;
    }
    closedir(dir);
    return count;
}

bool app_can_sd_delete_file(const char *path)
{
    if (!path) return false;
    return (remove(path) == 0);
}
