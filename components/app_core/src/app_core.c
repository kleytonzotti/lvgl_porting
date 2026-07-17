#include "app_core.h"
#include "app_io.h"
#include "app_can.h"
#include "app_ble.h"

void app_core_init(void)
{
    app_io_init();
    app_can_init();
    app_can_sniffer_start();  // fulltime: start collecting at boot
    app_ble_init();
}