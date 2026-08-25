#include "app_io.h"

float app_4_20ma_get_current_ma(void)
{
    static float value_ma = 4.0f;

    value_ma += 0.1f;

    if (value_ma > 20.0f) {
        value_ma = 4.0f;
    }

    return value_ma;
}

float app_4_20ma_get_percent(void)
{
    float ma = app_4_20ma_get_current_ma();

    if (ma < 4.0f) {
        ma = 4.0f;
    }

    if (ma > 20.0f) {
        ma = 20.0f;
    }

    return ((ma - 4.0f) / 16.0f) * 100.0f;
}