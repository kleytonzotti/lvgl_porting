#include "app_pedal_link.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define APP_PEDAL_UART_NUM      UART_NUM_1
#define APP_PEDAL_UART_BAUD     115200
#define APP_PEDAL_TASK_STACK    3072
#define APP_PEDAL_TASK_PRIORITY 5
#define APP_PEDAL_CMD_PERIOD_MS 1000   // reforça o modo mesmo sem mudança

// Pinos duplicados aqui (em vez de incluir bsp_waveshare_43.h) de propósito:
// esse componente não precisa da pilha inteira de LCD/touch/LVGL só pra
// saber 2 números de GPIO, e isso deixaria o app de testes (test_app/)
// arrastando dependência de tela por causa disso. Mantenha sincronizado
// com BSP_RS485_TX/BSP_RS485_RX em components/bsp_waveshare_43/include/bsp_waveshare_43.h
// — inclusive o aviso de conflito com o console: NÃO ligar nada aqui sem
// confirmar no esquemático físico da placa.
#define APP_PEDAL_RS485_TX_GPIO  43
#define APP_PEDAL_RS485_RX_GPIO  44

static const char *TAG = "APP_PEDAL";

static SemaphoreHandle_t    s_lock;
static TaskHandle_t         s_task;
static app_pedal_status_t   s_status;
static volatile app_pedal_mode_t s_current_mode  = APP_PEDAL_MODE_NORMAL;
static volatile bool             s_mode_dirty    = true;   // manda o modo assim que a task subir

// --- Parser da telemetria: [0xBB, pedal_pct, output_pct, fault_flags, checksum] ---
// s_rx_frame[0]=start(0xBB) [1]=pedal_pct [2]=output_pct [3]=fault_flags [4]=checksum
static uint8_t s_rx_frame[5];
static int     s_rx_idx = 0;

static void parser_reset(void) { s_rx_idx = 0; }

// Cria o mutex sob demanda — permite exercitar o parser (app_pedal_link_feed_bytes
// + app_pedal_link_get_status) sem precisar chamar app_pedal_link_init(), que
// mexe em hardware de verdade (UART/GPIO). É o que os testes automatizados usam.
static void ensure_lock(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
}

static void parser_feed_byte(uint8_t b)
{
    ensure_lock();
    if (s_rx_idx == 0) {
        if (b == APP_PEDAL_TELEMETRY_BYTE) {
            s_rx_frame[0] = b;
            s_rx_idx = 1;
        }
        return;
    }

    s_rx_frame[s_rx_idx++] = b;
    if (s_rx_idx < 5) return;

    uint8_t checksum = (uint8_t)(s_rx_frame[0] ^ s_rx_frame[1] ^ s_rx_frame[2] ^ s_rx_frame[3]);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (checksum == s_rx_frame[4]) {
        s_status.pedal_pct      = s_rx_frame[1];
        s_status.output_pct     = s_rx_frame[2];
        s_status.fault_flags    = s_rx_frame[3];
        s_status.frames_ok++;
        s_status.link_ok        = true;
        s_status.last_update_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    } else {
        s_status.frames_bad_checksum++;
    }
    xSemaphoreGive(s_lock);
    parser_reset();
}

static void send_heartbeat(void)
{
    uint8_t b = APP_PEDAL_HEARTBEAT_BYTE;
    uart_write_bytes(APP_PEDAL_UART_NUM, &b, 1);
}

static void send_mode_cmd(app_pedal_mode_t mode)
{
    uint8_t frame[3];
    frame[0] = APP_PEDAL_CMD_BYTE;
    frame[1] = (uint8_t)mode;
    frame[2] = (uint8_t)(frame[0] ^ frame[1]);
    uart_write_bytes(APP_PEDAL_UART_NUM, frame, sizeof(frame));
}

static void pedal_link_task(void *arg)
{
    (void)arg;
    uint8_t rx_buf[32];
    uint32_t last_heartbeat_ms = 0;
    uint32_t last_cmd_ms       = 0;

    for (;;) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (now - last_heartbeat_ms >= APP_PEDAL_HEARTBEAT_MS) {
            send_heartbeat();
            last_heartbeat_ms = now;
        }

        bool need_cmd = s_mode_dirty || (now - last_cmd_ms >= APP_PEDAL_CMD_PERIOD_MS);
        if (need_cmd) {
            send_mode_cmd(s_current_mode);
            s_mode_dirty = false;
            last_cmd_ms  = now;
        }

        int n = uart_read_bytes(APP_PEDAL_UART_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            app_pedal_link_feed_bytes(rx_buf, (size_t)n);
        }
    }
}

void app_pedal_link_feed_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        parser_feed_byte(data[i]);
    }
}

esp_err_t app_pedal_link_init(void)
{
    ensure_lock();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));

    uart_config_t cfg = {
        .baud_rate = APP_PEDAL_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_PEDAL_UART_NUM, 256, 256, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_PEDAL_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(APP_PEDAL_UART_NUM, APP_PEDAL_RS485_TX_GPIO, APP_PEDAL_RS485_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    if (!s_task) {
        BaseType_t ok = xTaskCreate(pedal_link_task, "pedal_link",
                                     APP_PEDAL_TASK_STACK, NULL,
                                     APP_PEDAL_TASK_PRIORITY, &s_task);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "app_pedal_link pronto (UART%d @ %d, TX=%d RX=%d)",
             APP_PEDAL_UART_NUM, APP_PEDAL_UART_BAUD, APP_PEDAL_RS485_TX_GPIO, APP_PEDAL_RS485_RX_GPIO);
    return ESP_OK;
}

void app_pedal_link_set_mode(app_pedal_mode_t mode)
{
    s_current_mode = mode;
    s_mode_dirty   = true;
}

void app_pedal_link_get_status(app_pedal_status_t *out)
{
    if (!out) return;
    ensure_lock();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
