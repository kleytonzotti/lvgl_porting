#pragma once
#include "lvgl.h"

// Para ativar fontes maiores:
// idf.py menuconfig → Component config → LVGL → Font usage
// Habilitar: 16, 20, 28, 36, 48

#if defined(CONFIG_LV_FONT_MONTSERRAT_48)
  #define ZOTTI_FONT_LOGO     (&lv_font_montserrat_48)
#else
  #define ZOTTI_FONT_LOGO     (&lv_font_montserrat_14)
#endif

#if defined(CONFIG_LV_FONT_MONTSERRAT_36)
  #define ZOTTI_FONT_HUGE     (&lv_font_montserrat_36)
#else
  #define ZOTTI_FONT_HUGE     (&lv_font_montserrat_14)
#endif

#if defined(CONFIG_LV_FONT_MONTSERRAT_28)
  #define ZOTTI_FONT_LARGE    (&lv_font_montserrat_28)
#else
  #define ZOTTI_FONT_LARGE    (&lv_font_montserrat_14)
#endif

#if defined(CONFIG_LV_FONT_MONTSERRAT_20)
  #define ZOTTI_FONT_MEDIUM   (&lv_font_montserrat_20)
#else
  #define ZOTTI_FONT_MEDIUM   (&lv_font_montserrat_14)
#endif

#if defined(CONFIG_LV_FONT_MONTSERRAT_16)
  #define ZOTTI_FONT_SMALL    (&lv_font_montserrat_16)
#else
  #define ZOTTI_FONT_SMALL    (&lv_font_montserrat_14)
#endif

#define ZOTTI_FONT_TINY       (&lv_font_montserrat_14)
