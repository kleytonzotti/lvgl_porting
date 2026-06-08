#include "app_core.h"
#include "app_io.h"
#include "app_can.h"

void app_core_init(void)
{
    app_io_init();
    app_can_init();
}