#include "zotti_brightness.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS  "zotti_bl"
#define NVS_KEY "pct"

// Sem NVS salva ainda (primeiro boot), começa mais escuro que o máximo de
// fábrica — foi pedido explicitamente pra "baixar o brilho" já de cara,
// não só deixar a opção disponível pro usuário achar sozinho.
#define ZOTTI_BRIGHTNESS_DEFAULT  70

// Opacidade máxima da superposição no piso de brilho (0..255) — nunca
// 255 (preto total): mesmo no mínimo, uma fração do conteúdo real
// continua visível através da superposição, senão pareceria tela
// desligada/travada em vez de "brilho baixo".
#define ZOTTI_BRIGHTNESS_OVERLAY_MAX_OPA  180

static const char *TAG = "ZOTTI_BRIGHTNESS";
static uint8_t     s_pct     = ZOTTI_BRIGHTNESS_MAX;
static lv_obj_t    *s_overlay = NULL;

static void apply_overlay(void)
{
    if (!s_overlay) return;

    uint32_t span     = ZOTTI_BRIGHTNESS_MAX - ZOTTI_BRIGHTNESS_MIN;
    uint32_t from_max  = ZOTTI_BRIGHTNESS_MAX - s_pct;   // 0 no MAX .. span no MIN
    uint32_t opa       = (span > 0) ? (from_max * ZOTTI_BRIGHTNESS_OVERLAY_MAX_OPA / span) : 0;

    lv_obj_set_style_bg_opa(s_overlay, (lv_opa_t)opa, 0);
}

void zotti_brightness_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    uint8_t pct = ZOTTI_BRIGHTNESS_DEFAULT;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY, &pct);
        nvs_close(h);
    }
    if (pct < ZOTTI_BRIGHTNESS_MIN || pct > ZOTTI_BRIGHTNESS_MAX) pct = ZOTTI_BRIGHTNESS_DEFAULT;
    s_pct = pct;

    // lv_layer_top() fica por cima de QUALQUER tela sem precisar adicionar
    // nada em cada uma — é global e sobrevive a troca de tela.
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    // CRÍTICO: sem isso, a superposição rouba TODO toque da tela real por
    // baixo (lv_obj_create é clicável por padrão na LVGL) — travaria o
    // touchscreen inteiro atrás de um retângulo invisível.
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    apply_overlay();
    ESP_LOGI(TAG, "Brilho inicial: %u%% (esmaecimento por software, ver zotti_brightness.h)",
             (unsigned)s_pct);
}

void zotti_brightness_set(uint8_t pct)
{
    if (pct < ZOTTI_BRIGHTNESS_MIN) pct = ZOTTI_BRIGHTNESS_MIN;
    if (pct > ZOTTI_BRIGHTNESS_MAX) pct = ZOTTI_BRIGHTNESS_MAX;

    s_pct = pct;
    apply_overlay();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY, pct);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Brilho ajustado para %u%%", (unsigned)pct);
}

uint8_t zotti_brightness_get(void)
{
    return s_pct;
}
