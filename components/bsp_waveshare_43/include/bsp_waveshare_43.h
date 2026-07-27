#ifndef BSP_WAVESHARE_43_H
#define BSP_WAVESHARE_43_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display resolution
#define BSP_LCD_H_RES               (800)
#define BSP_LCD_V_RES               (480)

// RGB panel clock
// Timing validado para o painel RGB da Waveshare 4.3B. Os porches, definidos
// em bsp_lcd_rgb.c, sao parte do protocolo do painel e nao devem ser
// substituidos por valores menores genericos.
#define BSP_LCD_PIXEL_CLOCK_HZ      (16 * 1000 * 1000)

// RGB pins
#define BSP_LCD_IO_VSYNC            (GPIO_NUM_3)
#define BSP_LCD_IO_HSYNC            (GPIO_NUM_46)
#define BSP_LCD_IO_DE               (GPIO_NUM_5)
#define BSP_LCD_IO_PCLK             (GPIO_NUM_7)
#define BSP_LCD_IO_DATA0            (GPIO_NUM_14)
#define BSP_LCD_IO_DATA1            (GPIO_NUM_38)
#define BSP_LCD_IO_DATA2            (GPIO_NUM_18)
#define BSP_LCD_IO_DATA3            (GPIO_NUM_17)
#define BSP_LCD_IO_DATA4            (GPIO_NUM_10)
#define BSP_LCD_IO_DATA5            (GPIO_NUM_39)
#define BSP_LCD_IO_DATA6            (GPIO_NUM_0)
#define BSP_LCD_IO_DATA7            (GPIO_NUM_45)
#define BSP_LCD_IO_DATA8            (GPIO_NUM_48)
#define BSP_LCD_IO_DATA9            (GPIO_NUM_47)
#define BSP_LCD_IO_DATA10           (GPIO_NUM_21)
#define BSP_LCD_IO_DATA11           (GPIO_NUM_1)
#define BSP_LCD_IO_DATA12           (GPIO_NUM_2)
#define BSP_LCD_IO_DATA13           (GPIO_NUM_42)
#define BSP_LCD_IO_DATA14           (GPIO_NUM_41)
#define BSP_LCD_IO_DATA15           (GPIO_NUM_40)

// I2C (backlight/touch/TF/CAN select via CH422G)
#define BSP_I2C_NUM                 (0)
#define BSP_I2C_SDA                 (GPIO_NUM_8)
#define BSP_I2C_SCL                 (GPIO_NUM_9)
#define BSP_I2C_FREQ_HZ             (400000)
#define BSP_I2C_TIMEOUT_MS          (1000)

// Board peripheral pins
#define BSP_CAN_TX                  (GPIO_NUM_15)
#define BSP_CAN_RX                  (GPIO_NUM_16)
#define BSP_SD_MOSI                 (GPIO_NUM_11)
#define BSP_SD_SCLK                 (GPIO_NUM_12)
#define BSP_SD_MISO                 (GPIO_NUM_13)

// ⚠️ CONFLITO CONHECIDO, NÃO LIGAR NADA AQUI SEM CONFIRMAR:
// A doc da Waveshare lista GPIO 43/44 como "RS485 onboard", mas o log de
// boot real deste projeto mostra "GPIO 44 and 43 are used as console UART
// I/O pins" — são os MESMOS pinos do console/USB de depuração usado pelo
// idf.py monitor (/dev/ttyACM0). Prováveis explicações: (a) o board
// multiplexa um único UART entre "modo debug" (USB) e "modo RS485"
// (terminal dedicado), não os dois ao mesmo tempo; (b) a doc do fabricante
// está descrevendo outra revisão de placa. De qualquer forma, ligar o
// módulo de pedal aqui SEM verificar o esquemático físico da sua placa
// pode causar dois periféricos discutindo o mesmo pino. Confirme no
// esquemático/serigrafia da placa antes de conectar qualquer fio físico —
// ver [[waveshare-43-gpio-map]] / ROADMAP.md.
#define BSP_RS485_TX                (GPIO_NUM_43)
#define BSP_RS485_RX                (GPIO_NUM_44)

// CH422G output bits
#define BSP_CH422_TP_RST_BIT        (1U << 1)
#define BSP_CH422_LCD_BL_BIT        (1U << 2)
#define BSP_CH422_SD_CS_BIT         (1U << 4)  // active low
#define BSP_CH422_CAN_SEL_BIT       (1U << 5)  // high selects CAN

// LCD bounce buffer — buffer pequeno em SRAM interna que o proprio driver
// esp_lcd_rgb_panel usa pra realimentar a FIFO de pixel do LCD em tempo
// real direto da PSRAM. E independente do avoid_tearing/direct_mode do
// esp_lvgl_port (bsp_lvgl_port.c) — aquele resolve "em qual framebuffer o
// LVGL pode desenhar sem correr com o scanout"; este resolve "o que
// acontece se o barramento PSRAM atrasar um instante durante o scanout".
// Estava desligado (0) achando que os dois mecanismos conflitavam — nao
// conflitam, sao ortogonais. Sem bounce buffer + framebuffer em PSRAM, uma
// soneca do barramento PSRAM (reconstruir a tela inteira faz bastante heap
// churn; CAN/SD/BLE tambem usam DMA) pode fazer a FIFO de pixel estourar e
// a imagem desalinhar horizontalmente NO MEIO do frame — o conteudo visual
// desliza pro lado mas o touch (I2C separado, GT911) continua certo, porque
// so a saida de video desincronizou, nao as coordenadas. Usuario confirmou
// que o embaralhamento acontece desde o inicio, em TODAS as telas, bem na
// hora de acessar/trocar de tela — exatamente o pico de carga (LVGL
// redesenha os 800x480 inteiros de uma vez numa troca de tela, disputando
// o barramento PSRAM com o resto do sistema). 10 linhas nao foi suficiente;
// subindo pra 20.
#define BSP_LCD_BOUNCE_LINES        (40)
#define BSP_LCD_BOUNCE_BUFFER_PX    (BSP_LCD_H_RES * BSP_LCD_BOUNCE_LINES)

// LVGL task
// O painel, com os timings abaixo e PCLK de 16 MHz, suporta ~39 Hz. 30 FPS
// deixa margem para o bounce buffer atender o DMA mesmo durante uma troca de
// tela, sem a latencia artificial que o limite anterior de 10 FPS introduzia.
#define BSP_LVGL_TARGET_FPS         (30)
#define BSP_LVGL_REFR_PERIOD_MS     (1000 / BSP_LVGL_TARGET_FPS)
#define BSP_LVGL_TICK_MS            (10)
#define BSP_LVGL_TASK_STACK_KB      (12)
#define BSP_LVGL_TASK_PRIORITY      (5)
#define BSP_LVGL_TASK_CORE          (1)
#define BSP_LVGL_TASK_MAX_DELAY_MS  (BSP_LVGL_REFR_PERIOD_MS)
#define BSP_LVGL_TASK_MIN_DELAY_MS  (1)
#define BSP_LVGL_BUFFER_LINES       (80)

esp_lcd_panel_handle_t bsp_lcd_get_panel_handle(void);

esp_err_t bsp_waveshare_43_init(void);

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_backlight_set(bool enable);
esp_err_t bsp_ch422_update(uint8_t mask, uint8_t value);
esp_err_t bsp_sdcard_select(bool selected);
esp_err_t bsp_can_set_selected(bool selected);
esp_err_t bsp_touch_reset_set(bool high);
esp_err_t bsp_lcd_rgb_init(void);
esp_err_t bsp_lvgl_init(void);

bool bsp_lvgl_lock(int timeout_ms);
typedef void (*bsp_touch_cb_t)(lv_coord_t x, lv_coord_t y, bool pressed);
void bsp_lvgl_unlock(void);
void bsp_touch_register_cb(bsp_touch_cb_t cb);
void bsp_lvgl_handler(void);



#ifdef __cplusplus
}
#endif

#endif
