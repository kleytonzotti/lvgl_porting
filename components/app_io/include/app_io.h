#ifndef APP_IO_H
#define APP_IO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_io_init(void);

float app_4_20ma_get_current_ma(void);
float app_4_20ma_get_percent(void);

void app_output_set(int index, bool state);
bool app_output_get(int index);

#ifdef __cplusplus
}
#endif

#endif