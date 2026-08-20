#include "app_core.h"
#include "app_io.h"
#include "app_can.h"
#include "app_ble.h"
#include "app_ecu.h"
#include "app_pedal_link.h"
#include "app_sim.h"
#include "app_dash_profile.h"
#include "app_dash_minmax.h"
#include "nvs_flash.h"

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_core_init(void)
{
    // BLE (calibracao PHY) e perfis/dashboard usam NVS. Ela precisa estar
    // pronta antes de inicializar qualquer um desses subsistemas.
    init_nvs();
    app_io_init();
    app_can_init();
    app_can_sniffer_start();  // fulltime: start collecting at boot
    app_ble_init();
    app_ecu_init();
    // app_pedal_link_init() DESLIGADO DE PROPÓSITO: ela remapeia de verdade
    // os pinos GPIO 43/44 pra uma segunda UART (UART1) logo no boot — e são
    // os MESMOS pinos do console/depuração (UART0, /dev/ttyACM0) que você
    // está usando agora. Isso compete pelo roteamento interno desses pinos
    // no chip mesmo sem nenhum fio físico ligado no módulo de pedal. Só
    // reativar depois de confirmar pinos de verdade livres (ver
    // app_pedal_link.c e ROADMAP.md §5).
    // app_pedal_link_init();
    app_sim_init();
    app_dash_profile_init();
    app_dash_minmax_init();
}
