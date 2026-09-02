#ifndef APP_MAP_H
#define APP_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// MAPAS de tunagem (injeção, ignição e sonda) — grade RPM x carga (MAP em
// kPa), mesmo conceito de VE table/ignition table/AFR target table do
// Speeduino/MegaSquirt/rusEFI já citados no ROADMAP.md §7.
//
// "Sonda" aqui é o mapa de LAMBDA ALVO pra malha fechada (closed-loop): a
// ECU usa isso pra saber que mistura perseguir em cada ponto de RPM/carga
// (rico sob carga alta/boost, próximo de estequiométrico em marcha lenta
// e cruzeiro) e corrige a injeção comparando com a leitura real da sonda
// lambda — não confundir com o CAMPO `lambda`/`lambda_x1000` do app_ecu
// (ROADMAP.md §4), que é a leitura AO VIVO da sonda, não um alvo.
//
// Isto é dado de CALIBRAÇÃO do motor — bem diferente da telemetria
// somente-leitura do app_ecu (ROADMAP.md §4): aqui é o PAINEL que ESCREVE
// na ECU. É uma exceção deliberada à "regra de ouro" do ROADMAP.md §1
// (painel nunca manda comando pra ECU), decidida em sessão explícita — não
// um acidente de arquitetura (ver ROADMAP.md §13 pra decisão registrada).
//
// A ECU é quem tem a palavra final: o firmware dela (fora deste repo) DEVE
// validar/clampar os valores recebidos e RECUSAR gravar um mapa novo com o
// motor girando (RPM > 0) — só aceita com motor parado. Isso é
// responsabilidade do firmware da ECU; aqui só definimos o protocolo, o
// editor e o cache local.
//
// Eixos compartilhados entre os três mapas (mesma grade RPM x kPa). Valores
// guardados em ponto fixo — inteiro não depende de como cada lado
// (ESP32 / STM32) serializa float, então não tem risco de divergência de
// representação entre os dois firmwares:
//   Injeção: décimos de milissegundo (ex.: 42 = 4.2ms).
//   Ignição: décimos de grau, com sinal (ex.: 150 = 15.0° APMS, -20 = -2.0°).
//   Sonda:   centésimos de lambda (ex.: 85 = 0.85 — rico; 100 = 1.00 —
//            estequiométrico). Centésimos (não décimos) porque a faixa
//            útil de lambda é bem mais estreita — precisão de 0.1 seria
//            grosseira demais pra malha fechada.
// ─────────────────────────────────────────────────────

#define APP_MAP_RPM_BINS   8
#define APP_MAP_LOAD_BINS  6

#define APP_MAP_INJ_MIN_TENTHS    5      //  0.5 ms
#define APP_MAP_INJ_MAX_TENTHS    300    // 30.0 ms
#define APP_MAP_IGN_MIN_TENTHS   (-100)  // -10.0 graus (retardo)
#define APP_MAP_IGN_MAX_TENTHS    600    //  60.0 graus (avanço)
#define APP_MAP_SONDA_MIN_X100    60     // lambda 0.60 (bem rico, plena carga/boost)
#define APP_MAP_SONDA_MAX_X100    130    // lambda 1.30 (bem pobre, economia)

typedef enum {
    APP_MAP_TABLE_INJECAO = 0,
    APP_MAP_TABLE_IGNICAO = 1,
    APP_MAP_TABLE_SONDA   = 2,
    APP_MAP_TABLE_COUNT,
} app_map_table_id_t;

typedef struct {
    uint16_t rpm_bins[APP_MAP_RPM_BINS];        // eixo RPM, crescente
    uint16_t load_kpa_bins[APP_MAP_LOAD_BINS];  // eixo carga (MAP), crescente
    int16_t  injecao[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];  // decimos de ms
    int16_t  ignicao[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];  // decimos de grau
    int16_t  sonda[APP_MAP_LOAD_BINS][APP_MAP_RPM_BINS];    // centesimos de lambda (alvo)
} app_map_set_t;

// --- Ciclo de vida / cache LOCAL (NVS do painel, ESP32) ---
// Isto é só o cache do painel pra reabrir a tela no último mapa editado —
// a cópia que manda de verdade é a gravada na FLASH da ECU (STM32), lida
// por ela sozinha a cada boot (ver o guia do firmware da ECU). Se o painel
// perder o NVS (reset de fábrica) mas a ECU não, a ECU continua com o mapa
// dela — os dois lados não dependem um do outro pra manter o próprio dado.
void app_map_init(void);
void app_map_get(app_map_set_t *out);
bool app_map_save_local(const app_map_set_t *in);

// Preenche 'out' com uma curva base "segura pra editar" — NÃO é calibração
// real de nenhum motor, é só um ponto de partida plausível pra não começar
// de uma tabela zerada. Sempre recalibrar no motor real antes de rodar.
void app_map_reset_default(app_map_set_t *out);

// ─────────────────────────────────────────────────────
// Protocolo BLE Painel -> ECU — baseado em UDS (ISO 14229-1), o protocolo
// automotivo padrão pra justamente isto: escrever um bloco de calibração
// na memória não-volátil de uma ECU. Não inventamos framing do zero desta
// vez — pesquisado e confirmado (ver STM32_ECU_SIMULADOR_BLE.md pras
// fontes): os Service IDs, subfunções e códigos de erro (NRC) abaixo são
// os REAIS do padrão, só a camada de transporte é nossa (BLE GATT, não
// CAN/ISO-TP — não existe um "UDS sobre BLE" oficial, essa parte é
// adaptação pragmática, deixada clara onde importa).
//
// **Por que UDS e não XCP** (o outro protocolo automotivo candidato,
// ASAM MCD-1 XCP): XCP foi feito pra TUNAGEM AO VIVO — motor rodando,
// malha de "muda um valor, vê o efeito na hora" (measure-and-adjust).
// Este projeto decidiu o oposto (ROADMAP.md §13): só aceita gravar um
// mapa novo com o motor PARADO. Isso é exatamente o caso de uso que o
// fluxo RequestDownload/TransferData/RequestTransferExit do UDS cobre —
// reflash de calibração em "modo de serviço", não ajuste em tempo real.
// A escolha de protocolo segue a decisão de segurança já tomada, não o
// contrário.
//
// Sequência completa (painel = "tester", ECU = "server", nomenclatura do
// próprio padrão):
//   1. DiagnosticSessionControl(programmingSession) — 0x10 0x02
//      A ECU DEVE recusar aqui (NRC conditionsNotCorrect) se o motor
//      estiver girando — é o primeiro e principal ponto de recusa.
//   2. SecurityAccess: requestSeed (0x27 0x01) -> a ECU manda um seed de
//      16 bits: sendKey (0x27 0x02 + key de 16 bits calculada do seed).
//      ⚠️ O algoritmo seed->key aqui (app_map_security_compute_key) é uma
//      transformação simples DE DEMONSTRAÇÃO, não criptografia real — o
//      objetivo é impedir que qualquer app BLE genérico escreva um mapa
//      por acidente/curiosidade, não resistir a um atacante que capturou
//      o tráfego. Trocar por algo mais forte antes de um uso sério.
//   3. RequestDownload (0x34) — declara qual tabela (endereço lógico de
//      16 bits, não endereço de flash real) e o tamanho (sempre
//      APP_MAP_SERIALIZED_LEN). A ECU responde com o maior bloco que
//      aceita por TransferData.
//   4. TransferData (0x36), repetido — cada um leva um
//      blockSequenceCounter (1 byte, começa em 1, incrementa) + uma fatia
//      do buffer serializado (ver app_map_serialize_table). Por padrão do
//      UDS: repetir o MESMO blockSequenceCounter do request anterior deve
//      ser aceito (é como o painel reenvia se perdeu a resposta) — só um
//      contador fora de ordem de verdade é erro.
//   5. RequestTransferExit (0x37) — carrega o CRC16 do buffer inteiro no
//      parametro (uso específico deste projeto, o padrão deixa esse campo
//      livre pro fabricante). A ECU recalcula, recheca o motor parado de
//      novo (pode ter ligado no meio da transferência) e só então grava
//      na flash.
//
// Resposta positiva = SID + 0x40 (ex.: 0x74 responde 0x34). Resposta
// negativa = sempre 0x7F seguido do SID original e um código de erro
// (NRC) — ver app_map_nrc_t. Cada PDU aqui vira UM write/notify BLE só
// (não precisamos do ISO-TP do CAN pra fragmentar: já dimensionamos cada
// TransferData pra caber no orçamento de um pacote ATT).
// ─────────────────────────────────────────────────────

#define APP_MAP_SID_DIAG_SESSION_CONTROL  0x10
#define APP_MAP_SID_SECURITY_ACCESS       0x27
#define APP_MAP_SID_REQUEST_DOWNLOAD      0x34
#define APP_MAP_SID_REQUEST_UPLOAD        0x35
#define APP_MAP_SID_TRANSFER_DATA         0x36
#define APP_MAP_SID_REQUEST_TRANSFER_EXIT 0x37
#define APP_MAP_SID_NEGATIVE_RESPONSE     0x7F
#define APP_MAP_POSITIVE_RESPONSE_OFFSET  0x40   // resposta positiva = SID pedido + isto

#define APP_MAP_SESSION_DEFAULT      0x01
#define APP_MAP_SESSION_PROGRAMMING  0x02

#define APP_MAP_SECURITY_REQUEST_SEED 0x01   // subfuncao impar = pede seed (nivel 1)
#define APP_MAP_SECURITY_SEND_KEY     0x02   // subfuncao par = manda key (nivel 1)

// Códigos de erro (Negative Response Code) — subconjunto real do ISO
// 14229-1 relevante pra esta troca (não é a tabela inteira do padrão,
// só os que esta ECU pode efetivamente responder).
typedef enum {
    APP_MAP_NRC_GENERAL_REJECT                   = 0x10,
    APP_MAP_NRC_SERVICE_NOT_SUPPORTED            = 0x11,
    APP_MAP_NRC_INCORRECT_LENGTH                 = 0x13,
    APP_MAP_NRC_CONDITIONS_NOT_CORRECT           = 0x22, // ex.: motor girando
    APP_MAP_NRC_REQUEST_SEQUENCE_ERROR           = 0x24, // servico fora de ordem (ex.: TransferData sem RequestDownload)
    APP_MAP_NRC_REQUEST_OUT_OF_RANGE             = 0x31, // celula fora da faixa, table_id invalido, etc.
    APP_MAP_NRC_SECURITY_ACCESS_DENIED           = 0x33,
    APP_MAP_NRC_INVALID_KEY                      = 0x35,
    APP_MAP_NRC_EXCEED_NUMBER_OF_ATTEMPTS        = 0x36,
    APP_MAP_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED     = 0x70,
    APP_MAP_NRC_GENERAL_PROGRAMMING_FAILURE      = 0x72, // ex.: CRC nao bateu, falha ao gravar flash
    APP_MAP_NRC_WRONG_BLOCK_SEQUENCE_COUNTER     = 0x73,
    APP_MAP_NRC_SUBFUNC_NOT_SUPPORTED_IN_SESSION = 0x7E, // ex.: tentou escrever ainda na sessao default
} app_map_nrc_t;

// Endereços lógicos (16 bits) usados no RequestDownload — NÃO são
// endereço de flash real, a ECU decide onde cada tabela mora de verdade.
// addressAndLengthFormatIdentifier fixo em 0x22 (2 bytes de endereço,
// 2 bytes de tamanho — ver app_map_build_request_download).
#define APP_MAP_ADDR_INJECAO  0x0000
#define APP_MAP_ADDR_IGNICAO  0x0001
#define APP_MAP_ADDR_SONDA    0x0002

// UUIDs do serviço BLE da ECU — mesmos documentados em
// STM32_ECU_SIMULADOR_BLE.md §4, fonte única compartilhada por
// components/app_map_debug_ble (servidor de debug) e components/app_ble
// (GATT client de verdade), pra nunca divergir entre os dois. Bytes em
// ordem "de fio" (little-endian/invertida em relação à string humana do
// UUID) — o jeito que BLE_UUID128_INIT() espera.
#define APP_MAP_BLE_SVC_UUID128 \
    0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x6b, 0x9f, \
    0x5d, 0x4a, 0x00, 0xec, 0x00, 0x10, 0x2b, 0x7a
#define APP_MAP_BLE_CHR_TELEMETRIA_UUID128 \
    0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x6b, 0x9f, \
    0x5d, 0x4a, 0x00, 0xec, 0x01, 0x10, 0x2b, 0x7a
#define APP_MAP_BLE_CHR_UDS_REQ_UUID128 \
    0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x6b, 0x9f, \
    0x5d, 0x4a, 0x00, 0xec, 0x02, 0x10, 0x2b, 0x7a
#define APP_MAP_BLE_CHR_UDS_RESP_UUID128 \
    0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x6b, 0x9f, \
    0x5d, 0x4a, 0x00, 0xec, 0x03, 0x10, 0x2b, 0x7a

// Tamanho do buffer serializado (eixos + a matriz de celulas de UMA
// tabela) que viaja fatiado dentro dos TransferData.
#define APP_MAP_SERIALIZED_LEN \
    (APP_MAP_RPM_BINS * 2 + APP_MAP_LOAD_BINS * 2 + APP_MAP_LOAD_BINS * APP_MAP_RPM_BINS * 2)

// Maior PDU que este protocolo gera (TransferData cheio: SID+BSC+16 bytes
// de dado = 18) — usado pra dimensionar buffers de pacote pelos dois lados.
#define APP_MAP_PDU_MAX_LEN  18

// CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF) — mesma conta que a ECU
// precisa fazer pra validar o RequestTransferExit. Exposta pra ser usada
// nos dois lados (painel e, futuramente, firmware da ECU) sem risco de
// divergir. Vetor de teste padrão: crc16("123456789") == 0x29B1.
uint16_t app_map_crc16(const uint8_t *data, size_t len);

// Serializa os eixos + a matriz de células de UMA tabela (injecao, ignicao
// OU sonda, conforme table_id) no formato que viaja fatiado no
// TransferData. 'out' precisa ter pelo menos APP_MAP_SERIALIZED_LEN bytes.
void app_map_serialize_table(const app_map_set_t *set, app_map_table_id_t table_id,
                              uint8_t *out);

// ── Construção dos PDUs (Painel -> ECU) ──
// Cada função monta UM PDU pronto pra virar um write BLE; 'out' deve ter
// espaço suficiente (ver o tamanho de cada uma nos comentários). Retornam
// o comprimento real escrito. Isto só MONTA bytes — não manda nada pelo
// ar; quem enviaria de verdade é app_map_send_to_ecu().

// [0x10][session]. out: 2 bytes.
uint8_t app_map_build_session_control(uint8_t session, uint8_t *out);

// [0x27][0x01] (pede seed, nivel 1). out: 2 bytes.
uint8_t app_map_build_security_seed_request(uint8_t *out);

// Calcula a key esperada a partir do seed recebido — MESMO algoritmo dos
// dois lados. ⚠️ Transformação simples de demonstração, não é
// criptografia de verdade (ver aviso grande acima).
uint16_t app_map_security_compute_key(uint16_t seed);

// [0x27][0x02][key_lo][key_hi]. out: 4 bytes.
uint8_t app_map_build_security_send_key(uint16_t seed, uint8_t *out);

// [0x34][dataFormatIdentifier=0x00][addrLenFmt=0x22][addr u16 LE][size u16 LE].
// size é sempre APP_MAP_SERIALIZED_LEN. out: 7 bytes.
uint8_t app_map_build_request_download(app_map_table_id_t table_id, uint8_t *out);

// Monta os PDUs TransferData (0x36) de UMA tabela inteira, um de cada vez,
// chamando on_pdu(ctx, pdu, len) na ordem — max_block_len é o que a ECU
// declarou aceitar na resposta do RequestDownload (tipicamente 16 bytes
// de dado útil, deixando espaço no pacote BLE pro cabeçalho SID+BSC).
typedef void (*app_map_pdu_cb_t)(void *ctx, const uint8_t *pdu, uint8_t len);
void app_map_build_transfer_data_pdus(const app_map_set_t *set, app_map_table_id_t table_id,
                                       uint8_t max_block_len,
                                       app_map_pdu_cb_t on_pdu, void *ctx);

// [0x37][crc16 LE do buffer serializado inteiro]. out: 3 bytes.
uint8_t app_map_build_transfer_exit(const app_map_set_t *set, app_map_table_id_t table_id,
                                     uint8_t *out);

// Decodifica uma resposta vinda da ECU — positiva (SID+0x40 [+dados]) ou
// negativa (0x7F [SID original] [NRC]). Retorna false se o PDU for curto
// demais pra ser válido.
typedef struct {
    bool    positive;
    uint8_t service_id;  // SID original (sem o +0x40 quando positiva)
    uint8_t nrc;          // só válido quando positive==false
} app_map_response_t;
bool app_map_parse_response(const uint8_t *pdu, uint8_t len, app_map_response_t *out);

// Transporte GATT real (ver components/app_ble) — mesmo padrão do debug
// sniffer acima: quem registrar aqui é chamado por app_map_send_to_ecu()/
// app_map_read_from_ecu() pra escrever cada PDU e esperar a resposta da
// ECU. app_map.c não depende de BLE/NimBLE — só desses dois ponteiros de
// função; é assim que o GATT client (app_ble.c) fica num componente
// separado sem criar dependência circular entre os dois.
//
// write_fn: escreve 'pdu' (len bytes) na characteristic "UDS Request" da
// ECU conectada agora. Deve retornar rápido (não espera resposta) —
// ESP_ERR_INVALID_STATE se não houver ECU conectada/pronta.
//
// wait_fn: bloqueia (até timeout_ms) esperando o PRÓXIMO notify recebido
// na characteristic "UDS Response", copiando pra 'out' (out_len
// preenchido com o tamanho real). ESP_ERR_TIMEOUT se nada chegar a tempo.
// Se nada registrar (app_ble.c só faz isso depois de conectar+descobrir o
// serviço da ECU), ambas ficam NULL e as funções abaixo se comportam como
// antes (ESP_ERR_NOT_SUPPORTED).
typedef esp_err_t (*app_map_transport_write_t)(const uint8_t *pdu, uint8_t len);
typedef esp_err_t (*app_map_transport_wait_t)(uint8_t *out, uint8_t *out_len, uint32_t timeout_ms);
void app_map_set_transport(app_map_transport_write_t write_fn, app_map_transport_wait_t wait_fn);

// Roda a sequência completa (de verdade, pelo ar, quando o transporte
// acima estiver registrado e a ECU conectada) pra gravar uma tabela na
// ECU: sessão de programação -> segurança -> RequestDownload ->
// TransferData (loop) -> RequestTransferExit, interpretando cada resposta
// antes de seguir pra próxima etapa (aborta no primeiro NRC negativo ou
// timeout). Sem transporte registrado ou sem ECU conectada, retorna
// ESP_ERR_NOT_SUPPORTED / ESP_ERR_INVALID_STATE — nunca assume sucesso.
esp_err_t app_map_send_to_ecu(const app_map_set_t *set, app_map_table_id_t table_id);

// DEBUG TEMPORÁRIO (removível, ver components/app_map_debug_ble/) — se
// registrado, toda chamada a app_map_send_to_ecu() entrega a ELE a
// sequência completa de PDUs que um GATT client de verdade mandaria
// (sessão, seed/key, RequestDownload, cada TransferData,
// RequestTransferExit), mesmo sem transporte real existir — permite
// inspecionar os bytes via nRF Connect (ou qualquer app BLE genérico) sem
// precisar da ECU. Não muda o retorno de app_map_send_to_ecu(); passar
// NULL desregistra. Sozinha esta função não faz nada — só existe pra
// alguém poder se inscrever.
void app_map_set_debug_sniffer(app_map_pdu_cb_t cb, void *ctx);

// ─────────────────────────────────────────────────────
// Leitura (ECU -> Painel) — RequestUpload (0x35), o par de leitura do
// RequestDownload. A ECU é a fonte de verdade do mapa (ela persiste na
// própria flash, ver ROADMAP.md §13) — o painel NUNCA deve assumir que o
// cache local em NVS é o dado atual sem antes tentar ler da ECU. Por isso
// toda abertura da tela "Mapas" tenta este fluxo primeiro; o cache local
// (app_map_get/app_map_save_local) só entra como QUEDA quando a ECU não
// responde (hoje sempre, porque falta o GATT client — ver
// app_map_read_from_ecu), e a tela avisa isso claramente.
//
// Diferença chave do UDS real entre download e upload: quem CARREGA o
// dado é a RESPOSTA do TransferData, não o pedido — o pedido de upload é
// só [0x36][BSC], pedindo "o próximo bloco"; o dado vem na resposta
// [0x76][BSC][dado...]. Sequência:
//   1. RequestUpload (0x35) — mesmo formato do RequestDownload, mesmos
//      endereços lógicos (APP_MAP_ADDR_*). Deliberadamente NÃO exige
//      sessão de programação nem SecurityAccess — ler é mais leve que
//      escrever, a exigência de segurança é só pro caminho que grava
//      (RequestDownload/TransferData/RequestTransferExit já documentado
//      acima).
//   2. TransferData (0x36), repetido — painel pede ([0x36][BSC], sem
//      dado), ECU responde com o bloco ([0x76][BSC][dado]).
//   3. RequestTransferExit (0x37), sem parâmetro no pedido — a ECU
//      responde com o CRC16 do que ELA mandou ([0x77][crc_lo][crc_hi]),
//      pro painel conferir contra o que reassemblou antes de aceitar
//      (se não bater, descarta a leitura, NÃO assume o cache local como
//      se fosse atual).
// ─────────────────────────────────────────────────────

// [0x35][0x00][0x22][addr u16 LE][size u16 LE] — mesmo formato do
// RequestDownload, só que pra pedir LEITURA. out: 7 bytes.
uint8_t app_map_build_request_upload(app_map_table_id_t table_id, uint8_t *out);

// [0x36][BSC], sem dado — pede o próximo bloco durante um upload
// (diferente do TransferData de escrita, que carrega dado no pedido).
// out: 2 bytes.
uint8_t app_map_build_transfer_data_request(uint8_t bsc, uint8_t *out);

// Extrai blockSequenceCounter e os bytes de dado de uma resposta positiva
// de TransferData de upload ([0x76][BSC][dado...]). 'out_data' aponta
// DENTRO de 'pdu' (não copia) — só válido enquanto 'pdu' existir. Retorna
// false se o PDU não for uma resposta positiva válida de TransferData.
bool app_map_parse_transfer_data_response(const uint8_t *pdu, uint8_t len, uint8_t *out_bsc,
                                            const uint8_t **out_data, uint8_t *out_data_len);

// [0x37], sem parâmetro — fecha um upload (diferente do fechamento de
// escrita, que carrega o CRC no PEDIDO; aqui o CRC vem na RESPOSTA da ECU,
// pro painel conferir o que recebeu). out: 1 byte.
uint8_t app_map_build_transfer_exit_read(uint8_t *out);

// Reconstrói os campos de UMA tabela dentro de 'out' a partir de um buffer
// serializado de APP_MAP_SERIALIZED_LEN bytes — inverso exato de
// app_map_serialize_table. Só escreve os campos da tabela indicada (mais
// os eixos, compartilhados); não mexe nas outras duas tabelas de 'out'.
void app_map_deserialize_table(const uint8_t *buf, app_map_table_id_t table_id, app_map_set_t *out);

// Lê (de verdade, pelo ar, quando o transporte acima estiver registrado e
// a ECU conectada) uma tabela da ECU pra 'out'. Sem transporte/ECU,
// retorna ESP_ERR_NOT_SUPPORTED / ESP_ERR_INVALID_STATE. Se e somente se
// retornar ESP_OK, 'out' foi preenchido com dado confirmado por CRC — o
// chamador NUNCA deve tratar 'out' como válido quando o retorno não é
// ESP_OK.
esp_err_t app_map_read_from_ecu(app_map_table_id_t table_id, app_map_set_t *out);

#ifdef __cplusplus
}
#endif

#endif
