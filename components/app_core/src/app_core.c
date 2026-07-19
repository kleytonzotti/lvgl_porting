#include "app_core.h"
#include "app_io.h"
#include "app_can.h"
#include "app_ble.h"
#include "app_ecu.h"
#include "app_pedal_link.h"
#include "app_sim.h"
#include "app_dash_profile.h"

void app_core_init(void)
{
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
}