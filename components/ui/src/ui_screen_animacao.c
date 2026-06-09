#include "ui.h"

void ui_screen_animacao_create(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Motor / CAN");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);
}