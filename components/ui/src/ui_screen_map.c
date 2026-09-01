#include "ui.h"
#include "app_map.h"
#include "app_ecu.h"
#include "zotti_theme.h"
#include "zotti_fonts.h"
#include "esp_log.h"

static const char *TAG = "UI_MAP";

// ─────────────────────────────────────────────────────
// MAPAS (injeção e ignição) — editor de tabela RPM x carga (kPa), estilo
// VE table/ignition table do Speeduino/MegaSquirt/rusEFI (ROADMAP.md §7).
//
// Grade em "heatmap" (azul=baixo … verde … amarelo … vermelho=alto), no
// estilo das telas de mapa da FuelTech (FTManager) e da Injepro: cada
// célula é um retângulo colorido pelo próprio valor (normalizado contra a
// faixa válida da tabela, não contra o mín/máx atual dos dados — assim a
// cor de uma célula não muda só porque você editou uma célula VIZINHA).
//
// ⚠️ IMPORTANTE (causa de um travamento/reboot real em hardware, 2026-08-31):
// este projeto configura LVGL com alocador PRÓPRIO de tamanho FIXO
// (`CONFIG_LV_USE_BUILTIN_MALLOC` + `CONFIG_LV_MEM_SIZE_KILOBYTES=64`) —
// só 64KB pra TODOS os objetos/estilos/timers de todo o app, à parte dos
// 8MB de PSRAM da placa (que a LVGL nem usa pra isso). A primeira versão
// desta tela criava 2 objetos por célula (retângulo + label filho) x 48
// células x 2 tabelas = ~192 objetos só na grade, cada um com 3-4
// propriedades de estilo LOCAIS (radius/border/padding) — estourava o
// pool de 64KB e a alocação falhava (`LV_USE_ASSERT_MALLOC=y` trava ou
// aborta, dependendo de onde bate). Duas correções aplicadas:
//   1. Cada célula agora é UM SÓ objeto (`lv_label_create` direto — label
//      é um lv_obj como outro qualquer, aceita bg_color/clique/etc. sem
//      precisar de um container em volta) — metade dos objetos.
//   2. Propriedades IDÊNTICAS em todas as células (radius, padding, fonte,
//      alinhamento, bg_opa) viraram um `lv_style_t` COMPARTILHADO
//      (`s_cell_style`, `lv_obj_add_style`) em vez de estilo local por
//      objeto — só bg_color e texto (que variam de verdade célula a
//      célula) continuam como override local.
// Ver `[DIAG-MAP-MEM]` no log (lv_mem_monitor) pra confirmar headroom.
//
// ⚠️ SEGUNDO travamento real (mesmo dia): a v1 do lazy-load (só construir
// uma aba na hora que o usuário troca pra ela) só resolvia o estouro na
// ABERTURA da tela — trocar de aba continuava só ACRESCENTANDO grades
// (Injeção→Ignição deixava as duas vivas, ~146 objetos; →Sonda deixava as
// três, ~219, voltando pro mesmo estouro do pool de 64KB). Corrigido em
// `tabview_changed_cb`: toda troca de aba agora DESTRÓI a grade de
// qualquer OUTRA aba construída antes de montar a nova — no máximo UMA
// aba com grade montada por vez, os valores continuam intactos em s_set.
//
// Optamos por montar a grade com objetos lv_obj comuns (não lv_table): o
// widget de tabela do LVGL não tem um jeito direto de pintar o fundo de
// uma célula por valor arbitrário.
//
// Fluxo: editar aqui só mexe em RAM (s_set); nada é persistido ou mandado
// pra ECU até o usuário clicar em "Salvar Mapa" — igual ao pedido original
// ("apenas salve quando clicar em salvar"). Salvar faz duas coisas:
//   1. app_map_save_local()  — cache do painel em NVS (ESP32), sobrevive a
//      reabrir esta tela, mas NÃO é a cópia que manda de verdade.
//   2. app_map_send_to_ecu() — tentativa de envio por BLE pra ECU (STM32)
//      gravar na flash dela. Hoje sempre retorna ESP_ERR_NOT_SUPPORTED
//      porque falta o GATT client de escrita (ver app_map.h) — a tela
//      mostra isso honestamente, não finge que enviou.
//
// Isto é uma EXCEÇÃO deliberada à regra "painel nunca escreve na ECU"
// (ROADMAP.md §1/§13) — decisão explícita, não acidente de arquitetura.
// A validação de segurança final (recusar gravar com motor girando etc.)
// é responsabilidade do firmware da ECU, fora deste repo.
// ─────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *cell[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];  // celula = o proprio label (heatmap)
    lv_obj_t *lbl_sel;
    int32_t   sel_row;   // -1 = nada selecionado; senao, indice 0-based direto em cell[][]
    int32_t   sel_col;
    bool      built;     // false = aba ainda nao construida (ver lazy-build em ui_screen_map_show)
} map_tab_ui_t;

typedef struct {
    app_map_table_id_t tid;
    int16_t             delta;
} step_ctx_t;

static app_map_set_t s_set;
static bool          s_dirty = false;

static map_tab_ui_t s_tab[APP_MAP_TABLE_COUNT];

static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_link   = NULL;
static lv_timer_t *s_link_timer = NULL;

// Contextos fixos dos botoes de passo — um por (tabela, delta), vida igual
// a do processo (nao precisam ser liberados).
static step_ctx_t s_step_ctx[APP_MAP_TABLE_COUNT][4];  // ordem: -10,-1,+1,+10

// Estilos compartilhados — ver o aviso grande acima sobre o pool de 64KB
// da LVGL. Tudo que e IGUAL em todas as celulas/cabecalhos mora aqui
// (custo de memoria pago uma vez so, nao por objeto); so bg_color e
// text_color (que variam de verdade) continuam como override local.
static lv_style_t s_cell_style;
static lv_style_t s_header_style;
static bool       s_styles_ready = false;

static void ensure_styles(void)
{
    if (s_styles_ready) return;

    lv_style_init(&s_cell_style);
    lv_style_set_radius(&s_cell_style, 3);
    lv_style_set_border_width(&s_cell_style, 0);
    lv_style_set_pad_all(&s_cell_style, 0);
    lv_style_set_pad_top(&s_cell_style, 7);   // aproxima centralizacao vertical do texto
    lv_style_set_bg_opa(&s_cell_style, LV_OPA_COVER);
    lv_style_set_text_font(&s_cell_style, ZOTTI_FONT_TINY);
    lv_style_set_text_align(&s_cell_style, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&s_header_style);
    lv_style_set_text_font(&s_header_style, ZOTTI_FONT_TINY);
    lv_style_set_text_color(&s_header_style, ZOTTI_GRAY);
    lv_style_set_text_align(&s_header_style, LV_TEXT_ALIGN_CENTER);

    s_styles_ready = true;
}

static void back_cb(lv_event_t *e) { LV_UNUSED(e); ui_nav(ui_menu_show); }

// Descreve cada uma das 3 tabelas — centraliza faixa/unidade/titulo num so
// lugar em vez de espalhar ternarios por refresh_cell/update_sel_label/
// salvar_cb/build_map_tab (ficava facil esquecer um lugar ao adicionar a
// tabela de Sonda).
typedef struct {
    int32_t     min_val;
    int32_t     max_val;
    float       divisor;    // valor bruto / divisor = valor mostrado ao usuario
    uint8_t     decimals;   // casas decimais na exibicao
    const char *unit;       // sufixo mostrado ("ms", "graus", "" pra lambda)
    const char *tab_title;  // titulo da aba no tabview
    const char *hint;       // dica de passo, mostrada na linha do editor
} map_table_desc_t;

static const map_table_desc_t k_table_desc[APP_MAP_TABLE_COUNT] = {
    [APP_MAP_TABLE_INJECAO] = {
        APP_MAP_INJ_MIN_TENTHS, APP_MAP_INJ_MAX_TENTHS, 10.0f, 1, "ms",
        "Injecao (ms)", "Passo: +-0.1ms / +-1.0ms",
    },
    [APP_MAP_TABLE_IGNICAO] = {
        APP_MAP_IGN_MIN_TENTHS, APP_MAP_IGN_MAX_TENTHS, 10.0f, 1, "graus",
        "Ignicao (graus)", "Passo: +-0.1 grau / +-1.0 grau",
    },
    [APP_MAP_TABLE_SONDA] = {
        APP_MAP_SONDA_MIN_X100, APP_MAP_SONDA_MAX_X100, 100.0f, 2, "",
        "Sonda (lambda)", "Alvo de lambda p/ malha fechada. Passo: +-0.01 / +-0.10",
    },
};

static int16_t *cell_ptr(app_map_table_id_t tid, int row, int col)
{
    switch (tid) {
    case APP_MAP_TABLE_INJECAO: return &s_set.injecao[row][col];
    case APP_MAP_TABLE_IGNICAO: return &s_set.ignicao[row][col];
    case APP_MAP_TABLE_SONDA:   return &s_set.sonda[row][col];
    default:                    return &s_set.injecao[row][col];  // nunca deveria acontecer
    }
}

static void table_range(app_map_table_id_t tid, int32_t *lo, int32_t *hi)
{
    *lo = k_table_desc[tid].min_val;
    *hi = k_table_desc[tid].max_val;
}

// Gradiente azul -> ciano -> verde -> amarelo -> vermelho, mesma linguagem
// visual das telas de mapa de bancadas de injeção conhecidas. 'dark_text'
// diz se o texto por cima desta cor deve ser preto (fundo claro, ex.:
// amarelo) ou branco (fundo escuro, ex.: azul/vermelho) — calculado pela
// luminância real da cor gerada, não por uma tabela fixa, então continua
// certo em qualquer ponto do gradiente.
static void heatmap_color(float t, lv_color_t *out_color, bool *out_dark_text)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    static const struct { float pos; uint8_t r, g, b; } stops[5] = {
        { 0.00f,  30,  60, 200 },  // azul
        { 0.25f,  20, 170, 190 },  // ciano
        { 0.50f,  20, 170,  60 },  // verde
        { 0.75f, 230, 200,   0 },  // amarelo
        { 1.00f, 220,  40,  30 },  // vermelho
    };

    int i = 0;
    while (i < 3 && t > stops[i + 1].pos) i++;
    float span = stops[i + 1].pos - stops[i].pos;
    float f = (span > 0.0001f) ? (t - stops[i].pos) / span : 0.0f;

    uint8_t r = (uint8_t)((float)stops[i].r + f * ((float)stops[i + 1].r - (float)stops[i].r));
    uint8_t g = (uint8_t)((float)stops[i].g + f * ((float)stops[i + 1].g - (float)stops[i].g));
    uint8_t b = (uint8_t)((float)stops[i].b + f * ((float)stops[i + 1].b - (float)stops[i].b));

    *out_color = lv_color_make(r, g, b);
    uint32_t luminance = (299u * r + 587u * g + 114u * b) / 1000u;
    *out_dark_text = (luminance > 150u);
}

static void refresh_cell(app_map_table_id_t tid, int row, int col)
{
    map_tab_ui_t *tab = &s_tab[tid];
    lv_obj_t *cell = tab->cell[row][col];
    if (!cell) return;

    int16_t v = *cell_ptr(tid, row, col);
    int32_t lo, hi;
    table_range(tid, &lo, &hi);
    float t = (float)(v - lo) / (float)(hi - lo);

    lv_color_t bg;
    bool dark_text;
    heatmap_color(t, &bg, &dark_text);

    lv_obj_set_style_bg_color(cell, bg, 0);
    lv_obj_set_style_text_color(cell, dark_text ? lv_color_black() : lv_color_white(), 0);
    lv_label_set_text_fmt(cell, "%.*f", k_table_desc[tid].decimals, (double)v / k_table_desc[tid].divisor);
}

static void update_sel_label(app_map_table_id_t tid)
{
    map_tab_ui_t *tab = &s_tab[tid];
    if (!tab->lbl_sel) return;

    if (tab->sel_row < 0 || tab->sel_col < 0) {
        lv_label_set_text(tab->lbl_sel, "Toque numa celula do grafico pra selecionar");
        return;
    }

    uint16_t rpm  = s_set.rpm_bins[tab->sel_col];
    uint16_t load = s_set.load_kpa_bins[tab->sel_row];
    int16_t  v    = *cell_ptr(tid, tab->sel_row, tab->sel_col);
    const map_table_desc_t *desc = &k_table_desc[tid];

    lv_label_set_text_fmt(tab->lbl_sel, "%u RPM / %u kPa  =  %.*f %s",
                           (unsigned)rpm, (unsigned)load,
                           desc->decimals, (double)v / desc->divisor, desc->unit);
}

static void select_cell(app_map_table_id_t tid, int row, int col)
{
    map_tab_ui_t *tab = &s_tab[tid];

    if (tab->sel_row >= 0 && tab->sel_col >= 0) {
        lv_obj_t *prev = tab->cell[tab->sel_row][tab->sel_col];
        if (prev) lv_obj_set_style_border_width(prev, 0, 0);
    }

    tab->sel_row = row;
    tab->sel_col = col;

    lv_obj_t *cur = tab->cell[row][col];
    if (cur) {
        lv_obj_set_style_border_width(cur, 3, 0);
        lv_obj_set_style_border_color(cur, ZOTTI_WHITE, 0);
    }
    update_sel_label(tid);
}

static void cell_click_cb(lv_event_t *e)
{
    uintptr_t id = (uintptr_t)lv_event_get_user_data(e);
    app_map_table_id_t tid = (app_map_table_id_t)(id / 1000u);
    int row = (int)((id % 1000u) / 100u);
    int col = (int)(id % 100u);
    select_cell(tid, row, col);
}

static void step_cb(lv_event_t *e)
{
    step_ctx_t *ctx = (step_ctx_t *)lv_event_get_user_data(e);
    map_tab_ui_t *tab = &s_tab[ctx->tid];
    if (tab->sel_row < 0 || tab->sel_col < 0) return;

    int16_t *cell = cell_ptr(ctx->tid, tab->sel_row, tab->sel_col);
    int32_t  v    = (int32_t)*cell + ctx->delta;

    int32_t lo, hi;
    table_range(ctx->tid, &lo, &hi);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *cell = (int16_t)v;

    refresh_cell(ctx->tid, tab->sel_row, tab->sel_col);
    update_sel_label(ctx->tid);

    s_dirty = true;
    if (s_lbl_status) {
        lv_label_set_text(s_lbl_status, LV_SYMBOL_WARNING "  Alteracoes nao salvas");
        lv_obj_set_style_text_color(s_lbl_status, ZOTTI_YELLOW, 0);
    }
}

static void salvar_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    bool ok_local = app_map_save_local(&s_set);

    bool all_sent_ok = true;
    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        if (app_map_send_to_ecu(&s_set, (app_map_table_id_t)t) != ESP_OK) all_sent_ok = false;
    }
    s_dirty = false;

    if (!s_lbl_status) return;

    if (!ok_local) {
        lv_label_set_text(s_lbl_status, LV_SYMBOL_WARNING "  Falha ao salvar localmente (NVS)");
        lv_obj_set_style_text_color(s_lbl_status, ZOTTI_RED, 0);
    } else if (all_sent_ok) {
        lv_label_set_text(s_lbl_status, LV_SYMBOL_OK "  Mapa salvo e enviado para a ECU");
        lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GREEN, 0);
    } else {
        lv_label_set_text(s_lbl_status,
            LV_SYMBOL_WARNING "  Salvo localmente. Envio BLE indisponivel (GATT client de escrita ainda nao implementado)");
        lv_obj_set_style_text_color(s_lbl_status, ZOTTI_YELLOW, 0);
    }
}

// ─────────────────────────────────────────────────────
// Construcao de uma aba (Injecao ou Ignicao) — grade heatmap + editor.
// ─────────────────────────────────────────────────────
// Celula = UM SO objeto (label direto, sem container em volta) — ver o
// aviso grande no topo do arquivo sobre o pool de 64KB da LVGL. As
// propriedades iguais em toda celula vem do estilo compartilhado
// s_cell_style; so bg_color/text_color/texto (que variam) sao locais.
static void build_cell(lv_obj_t *parent, app_map_table_id_t tid, int row, int col,
                        int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *cell = lv_label_create(parent);
    lv_obj_add_style(cell, &s_cell_style, 0);
    lv_obj_set_size(cell, w, h);
    lv_obj_set_pos(cell, x, y);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

    uintptr_t id = (uintptr_t)tid * 1000u + (uintptr_t)row * 100u + (uintptr_t)col;
    lv_obj_add_event_cb(cell, cell_click_cb, LV_EVENT_CLICKED, (void *)id);

    s_tab[tid].cell[row][col] = cell;
}

static void build_map_tab(lv_obj_t *parent, app_map_table_id_t tid)
{
    ensure_styles();
    const map_table_desc_t *desc = &k_table_desc[tid];

    map_tab_ui_t *tab = &s_tab[tid];
    tab->sel_row = tab->sel_col = -1;

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 780, 230);
    lv_obj_set_pos(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t hdr_col_w = 68;
    const int32_t hdr_row_h = 26;
    const int32_t cell_w    = (780 - hdr_col_w) / APP_MAP_RPM_BINS;
    const int32_t cell_h    = (230 - hdr_row_h) / APP_MAP_LOAD_BINS;

    lv_obj_t *corner = lv_label_create(grid);
    lv_obj_add_style(corner, &s_header_style, 0);
    lv_label_set_text(corner, "kPa\\RPM");
    lv_obj_set_pos(corner, 2, 4);

    // Cabecalho RPM (topo) — eixo das colunas.
    for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
        lv_obj_t *lbl = lv_label_create(grid);
        lv_obj_add_style(lbl, &s_header_style, 0);
        lv_label_set_text_fmt(lbl, "%u", (unsigned)s_set.rpm_bins[c]);
        lv_obj_set_size(lbl, cell_w, hdr_row_h);
        lv_obj_set_pos(lbl, hdr_col_w + c * cell_w, 0);
    }

    // Cabecalho kPa (esquerda) — eixo das linhas.
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        lv_obj_t *lbl = lv_label_create(grid);
        lv_obj_add_style(lbl, &s_header_style, 0);
        lv_label_set_text_fmt(lbl, "%u", (unsigned)s_set.load_kpa_bins[r]);
        lv_obj_set_size(lbl, hdr_col_w, cell_h);
        lv_obj_set_pos(lbl, 0, hdr_row_h + r * cell_h);
    }

    // Grade de celulas coloridas (heatmap) — o "grafico" propriamente dito.
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            build_cell(grid, tid, r, c,
                       hdr_col_w + c * cell_w + 1, hdr_row_h + r * cell_h + 1,
                       cell_w - 2, cell_h - 2);
            refresh_cell(tid, r, c);
        }
    }

    // Linha do editor de celula selecionada.
    lv_obj_t *editor = lv_obj_create(parent);
    lv_obj_set_size(editor, 780, 90);
    lv_obj_set_pos(editor, 0, 240);
    lv_obj_set_style_bg_opa(editor, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(editor, 0, 0);
    lv_obj_clear_flag(editor, LV_OBJ_FLAG_SCROLLABLE);

    tab->lbl_sel = lv_label_create(editor);
    lv_label_set_text(tab->lbl_sel, "Toque numa celula do grafico pra selecionar");
    lv_obj_set_style_text_font(tab->lbl_sel, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(tab->lbl_sel, ZOTTI_WHITE, 0);
    lv_obj_align(tab->lbl_sel, LV_ALIGN_TOP_LEFT, 5, 5);

    lv_obj_t *lbl_unit = lv_label_create(editor);
    lv_label_set_text(lbl_unit, desc->hint);
    lv_obj_set_style_text_font(lbl_unit, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_unit, ZOTTI_GRAY, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_TOP_LEFT, 5, 32);

    // Rotulo de cada botao calculado a partir do divisor da propria tabela
    // (ex.: delta bruto 10 vira "+1.00" pra ms/graus mas "+0.10" pra lambda)
    // — evita precisar de um array de textos por tabela.
    static const int16_t deltas[4] = {-10, -1, 1, 10};
    int32_t btn_x[4] = {5, 130, 255, 380};
    for (int i = 0; i < 4; i++) {
        s_step_ctx[tid][i].tid   = tid;
        s_step_ctx[tid][i].delta = deltas[i];

        lv_obj_t *btn = lv_btn_create(editor);
        lv_obj_set_size(btn, 115, 40);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, btn_x[i], 46);
        lv_obj_set_style_bg_color(btn, (deltas[i] < 0) ? ZOTTI_GRAY_DARK : ZOTTI_ACCENT_DIM, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_add_event_cb(btn, step_cb, LV_EVENT_CLICKED, &s_step_ctx[tid][i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%+.2f", (double)deltas[i] / desc->divisor);
        lv_obj_set_style_text_font(lbl, ZOTTI_FONT_SMALL, 0);
        lv_obj_center(lbl);
    }

    tab->built = true;
}

static void link_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!s_lbl_link) return;

    app_ecu_status_t st;
    app_ecu_get_status(&st);
    bool connected = (st.state == APP_ECU_STATE_CONNECTED);
    lv_label_set_text(s_lbl_link, connected
        ? LV_SYMBOL_BLUETOOTH "  ECU conectada"
        : LV_SYMBOL_BLUETOOTH "  ECU desconectada");
    lv_obj_set_style_text_color(s_lbl_link, connected ? ZOTTI_GREEN : ZOTTI_GRAY, 0);
}

// Zera o rastreamento de UMA aba (nao mexe nos objetos LVGL em si — quem
// chama e responsavel por ja ter deletado/limpado o container antes, ou
// estar chamando isto so porque a tela inteira esta sendo destruida).
static void reset_tab_state(int t)
{
    for (int r = 0; r < APP_MAP_LOAD_BINS; r++) {
        for (int c = 0; c < APP_MAP_RPM_BINS; c++) {
            s_tab[t].cell[r][c] = NULL;
        }
    }
    s_tab[t].lbl_sel = NULL;
    s_tab[t].sel_row = s_tab[t].sel_col = -1;
    s_tab[t].built   = false;
}

static void screen_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_link_timer) { lv_timer_delete(s_link_timer); s_link_timer = NULL; }
    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) reset_tab_state(t);
    s_lbl_status = NULL;
    s_lbl_link   = NULL;
}

// ⚠️ SEGUNDA causa real de travamento/reboot em hardware (2026-08-31,
// mesmo dia da primeira): a primeira versao deste lazy-build so
// CONSTRUIA a aba nova ao trocar, nunca DESTRUIA a aba de onde o usuario
// estava saindo — Injecao -> Ignicao deixava as DUAS grades (~146
// objetos) vivas ao mesmo tempo; Ignicao -> Sonda deixava as TRES
// (~219), voltando pro mesmo estouro do pool de 64KB que a v1 da tela
// tinha (so que agora espalhado por troca de aba em vez de tudo de uma
// vez). Correcao: cada troca de aba agora DESTROI a grade de qualquer
// OUTRA aba que estivesse construida (lv_obj_clean no container dela)
// antes de construir a nova — no maximo UMA aba com grade montada por
// vez, nao importa quantas o usuario visite. Os VALORES continuam intactos
// em s_set (isto so destroi objetos de UI, nao dado) — reabrir uma aba
// visitada antes so reconstroi a grade com os mesmos valores.
static void tabview_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv  = lv_event_get_target(e);
    uint32_t  idx = lv_tabview_get_tab_active(tv);
    if (idx >= APP_MAP_TABLE_COUNT) return;

    lv_obj_t *content = lv_tabview_get_content(tv);

    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        if (t == (int)idx || !s_tab[t].built) continue;
        lv_obj_t *other = lv_obj_get_child(content, t);
        if (other) lv_obj_clean(other);   // libera os ~70+ objetos da aba anterior
        reset_tab_state(t);
    }

    if (s_tab[idx].built) return;   // ja e a aba ativa e ja esta construida

    lv_obj_t *tab_container = lv_obj_get_child(content, (int32_t)idx);
    if (!tab_container) return;

    build_map_tab(tab_container, (app_map_table_id_t)idx);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "[DIAG-MAP-MEM] aba %u construida sob demanda — pool: %u%% usado, maior bloco livre=%u bytes",
             (unsigned)idx, (unsigned)mon.used_pct, (unsigned)mon.free_biggest_size);
}

void ui_screen_map_show(void)
{
    app_map_get(&s_set);
    s_dirty = false;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ZOTTI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    // Header.
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, ZOTTI_BG_HEADER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 90, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, ZOTTI_ACCENT_DIM, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " MENU");
    lv_obj_set_style_text_font(lbl_back, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_back);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "MAPAS (INJECAO / IGNICAO / SONDA)");
    lv_obj_set_style_text_font(lbl_title, ZOTTI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_title, ZOTTI_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_lbl_link = lv_label_create(header);
    lv_label_set_text(s_lbl_link, LV_SYMBOL_BLUETOOTH "  ECU desconectada");
    lv_obj_set_style_text_font(s_lbl_link, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_link, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_link, LV_ALIGN_RIGHT_MID, -10, 0);

    // Barra de status + Salvar.
    lv_obj_t *action_bar = lv_obj_create(scr);
    lv_obj_set_size(action_bar, 800, 40);
    lv_obj_set_pos(action_bar, 0, 40);
    lv_obj_set_style_bg_color(action_bar, ZOTTI_BG_CARD, 0);
    lv_obj_set_style_border_width(action_bar, 0, 0);
    lv_obj_clear_flag(action_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status = lv_label_create(action_bar);
    lv_label_set_text(s_lbl_status, "Edite uma celula e clique em Salvar Mapa");
    lv_obj_set_style_text_font(s_lbl_status, ZOTTI_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_lbl_status, ZOTTI_GRAY, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *btn_salvar = lv_btn_create(action_bar);
    lv_obj_set_size(btn_salvar, 180, 30);
    lv_obj_align(btn_salvar, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_salvar, ZOTTI_GREEN, 0);
    lv_obj_set_style_radius(btn_salvar, 6, 0);
    lv_obj_add_event_cb(btn_salvar, salvar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_salvar = lv_label_create(btn_salvar);
    lv_label_set_text(lbl_salvar, LV_SYMBOL_SAVE " Salvar Mapa");
    lv_obj_set_style_text_font(lbl_salvar, ZOTTI_FONT_TINY, 0);
    lv_obj_center(lbl_salvar);

    // Tabview: Injecao | Ignicao | Sonda. So a primeira aba (a que abre por
    // padrao) e construida agora — as outras ficam pra tabview_changed_cb
    // construir na hora que o usuario trocar pra elas pela primeira vez
    // (ver o aviso grande no topo do arquivo sobre o pool de 64KB da LVGL).
    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, 800, 400);
    lv_obj_set_pos(tv, 0, 80);
    lv_tabview_set_tab_bar_size(tv, 34);
    lv_obj_set_style_bg_color(tv, ZOTTI_BG, 0);

    lv_obj_t *tab_first = NULL;
    for (int t = 0; t < APP_MAP_TABLE_COUNT; t++) {
        lv_obj_t *tab = lv_tabview_add_tab(tv, k_table_desc[t].tab_title);
        if (t == 0) tab_first = tab;
    }
    lv_obj_add_event_cb(tv, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    build_map_tab(tab_first, (app_map_table_id_t)0);

    s_link_timer = lv_timer_create(link_timer_cb, 500, NULL);
    link_timer_cb(NULL);

    ui_screen_load(scr);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "Mapas criado — [DIAG-MAP-MEM] LVGL pool: %u%% usado, maior bloco livre=%u bytes",
             (unsigned)mon.used_pct, (unsigned)mon.free_biggest_size);
}
