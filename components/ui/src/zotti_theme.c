#include "zotti_theme.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS  "zotti_thm"
#define NVS_KEY "id"

static const char *TAG = "ZOTTI_THEME";

lv_color_t g_zotti_bg;
lv_color_t g_zotti_bg_card;
lv_color_t g_zotti_bg_header;
lv_color_t g_zotti_accent;
lv_color_t g_zotti_accent_dim;
lv_color_t g_zotti_white;
lv_color_t g_zotti_gray;
lv_color_t g_zotti_gray_dark;
lv_color_t g_zotti_green;
lv_color_t g_zotti_yellow;
lv_color_t g_zotti_red;
lv_color_t g_zotti_border;

typedef struct {
    const char *name;
    uint32_t bg, bg_card, bg_header;
    uint32_t accent, accent_dim;
    uint32_t white, gray, gray_dark;
    uint32_t green, yellow, red;
    uint32_t border;
} zotti_palette_t;

static const zotti_palette_t k_palettes[ZOTTI_THEME_COUNT] = {
    // TEMA1 — visual original (azul/ciano escuro, tecnologico). Valores
    // identicos aos que estavam fixos em zotti_theme.h antes do tema
    // virar trocavel — de proposito, pra nao mudar nada de quem ja usava.
    [ZOTTI_THEME_1] = {
        .name = "TEMA1",
        .bg = 0x0A1628, .bg_card = 0x112240, .bg_header = 0x0D1E38,
        .accent = 0x00BFFF, .accent_dim = 0x005580,
        .white = 0xFFFFFF, .gray = 0x7B9EB8, .gray_dark = 0x2A4A6A,
        .green = 0x00DD55, .yellow = 0xFFCC00, .red = 0xFF3333,
        .border = 0x1E3A5F,
    },
    // TEMA2 — esportivo: preto/vermelho, alto contraste, estilo FuelTech/
    // painel de competicao.
    [ZOTTI_THEME_2] = {
        .name = "TEMA2",
        .bg = 0x0A0A0A, .bg_card = 0x1A1414, .bg_header = 0x160A0A,
        .accent = 0xFF3D00, .accent_dim = 0x7A1D00,
        .white = 0xFFFFFF, .gray = 0x9E9E9E, .gray_dark = 0x333333,
        .green = 0x00E676, .yellow = 0xFFD600, .red = 0xFF1744,
        .border = 0x3A2A2A,
    },
    // TEMA3 — classico: ambar/creme, tom quente de painel analogico antigo.
    [ZOTTI_THEME_3] = {
        .name = "TEMA3",
        .bg = 0x14100C, .bg_card = 0x241E16, .bg_header = 0x1C1710,
        .accent = 0xE8A33D, .accent_dim = 0x6B4E1F,
        .white = 0xF2E8D5, .gray = 0xA89A82, .gray_dark = 0x3A3226,
        .green = 0x6FBF73, .yellow = 0xE8C547, .red = 0xC94C4C,
        .border = 0x4A3F2E,
    },
};

static zotti_theme_id_t s_current = ZOTTI_THEME_1;

static void apply_palette(const zotti_palette_t *p)
{
    g_zotti_bg          = lv_color_hex(p->bg);
    g_zotti_bg_card     = lv_color_hex(p->bg_card);
    g_zotti_bg_header   = lv_color_hex(p->bg_header);
    g_zotti_accent      = lv_color_hex(p->accent);
    g_zotti_accent_dim  = lv_color_hex(p->accent_dim);
    g_zotti_white       = lv_color_hex(p->white);
    g_zotti_gray        = lv_color_hex(p->gray);
    g_zotti_gray_dark   = lv_color_hex(p->gray_dark);
    g_zotti_green       = lv_color_hex(p->green);
    g_zotti_yellow      = lv_color_hex(p->yellow);
    g_zotti_red         = lv_color_hex(p->red);
    g_zotti_border      = lv_color_hex(p->border);
}

void zotti_theme_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    uint8_t id = ZOTTI_THEME_1;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY, &id);
        nvs_close(h);
    }
    if (id >= ZOTTI_THEME_COUNT) id = ZOTTI_THEME_1;

    s_current = (zotti_theme_id_t)id;
    apply_palette(&k_palettes[s_current]);
    ESP_LOGI(TAG, "Tema ativo: %s", k_palettes[s_current].name);
}

void zotti_theme_set(zotti_theme_id_t id)
{
    if (id >= ZOTTI_THEME_COUNT) return;
    s_current = id;
    apply_palette(&k_palettes[s_current]);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY, (uint8_t)id);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Tema trocado para: %s", k_palettes[s_current].name);
}

zotti_theme_id_t zotti_theme_get(void)
{
    return s_current;
}

const char *zotti_theme_name(zotti_theme_id_t id)
{
    if (id >= ZOTTI_THEME_COUNT) return "?";
    return k_palettes[id].name;
}
