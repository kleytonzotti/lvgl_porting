#include "app_io.h"

#define APP_OUTPUT_COUNT 3

static bool s_outputs[APP_OUTPUT_COUNT] = {false, false, false};

void app_output_set(int index, bool state)
{
    if (index < 0 || index >= APP_OUTPUT_COUNT) {
        return;
    }

    s_outputs[index] = state;
}

bool app_output_get(int index)
{
    if (index < 0 || index >= APP_OUTPUT_COUNT) {
        return false;
    }

    return s_outputs[index];
}