# Guia — Simulador de ECU via BLE no STM32WB5MM-DK

> **Como usar este arquivo:** copie o conteúdo inteiro e cole numa conversa
> nova com o Claude (Claude Code, de preferência, já dentro da pasta do seu
> projeto STM32CubeIDE). Ele foi escrito pra ser lido por outra sessão de
> IA sem contexto prévio — por isso repete algumas coisas que você (o
> zotti) já sabe. Peça pra seguir passo a passo, confirmando cada etapa
> antes de ir pra próxima, em vez de gerar tudo de uma vez.
>
> Este guia foi gerado a partir da leitura completa do firmware do painel
> (`lvgl_porting`, ESP32-S3, LVGL9) que vai se conectar nessa ECU simulada.
> O protocolo descrito aqui (§3) **já está implementado e testado do lado
> do painel** — não é uma proposta em aberto, é a especificação que a ECU
> precisa respeitar byte a byte pra funcionar.

## 0. Contexto — o que já existe e por que isso importa

O painel (`lvgl_porting`) já tem:
- Um parser completo e testado (Unity) pra telemetria da ECU via BLE
  notify (`components/app_ecu/`) — só falta o lado da ECU existir de
  verdade e mandar os bytes certos.
- Uma tela "MONITOR ECU BLE" (`ui_screen_ecu.c`) que mostra RPM, MAP, TPS,
  ECT, IAT, bateria, lambda/AFR e estado de conexão.
- Um editor de mapas de tunagem (injeção/ignição), tela "MAPAS"
  (`ui_screen_map.c`) — o usuário edita uma tabela RPM x carga (kPa) na
  tela e manda pra ECU gravar. Isso é uma característica de ESCRITA nova
  (protocolo em `components/app_map/`), diferente da telemetria (que é só
  leitura).
- Uma regra de arquitetura importante, documentada e **para ser
  respeitada pelo firmware da ECU também**: o painel nunca deveria
  precisar confiar cegamente em nada que ele manda. A ECU é quem valida.

**Sua tarefa (a da ECU):** implementar um firmware no STM32WB5MM-DK que:
1. Simula sensores de motor via potenciômetros (não tem motor de verdade
   ligado — isto é uma bancada de teste de protocolo/BLE, não uma ECU real
   funcional ainda).
2. Manda esses valores por BLE notify pro painel, no formato exato do §3.1.
3. Recebe mapas de tunagem via BLE write, valida, grava na flash interna,
   e responde o status — formato exato do §3.2.
4. Ao ligar, lê o mapa salvo da flash (se existir) antes de qualquer outra
   coisa.
5. Espelha tudo (valores simulados, frames enviados/recebidos, decisões de
   validação) numa UART de debug, texto legível, pra você acompanhar num
   terminal serial enquanto testa.

## 1. Objetivo e escopo (leia antes de começar)

Isto é um **simulador de bancada**, não uma ECU real. O objetivo é validar
o protocolo BLE e a tela do painel com valores controláveis via
potenciômetro, sem precisar de motor, bicos injetores, bobinas nem sensores
automotivos de verdade. Ler um motor real (roda fônica, bicos, bobinas) é
um projeto de hardware bem maior, já mapeado em `ROADMAP.md` §7 do
repositório do painel (chips automotivos AEC-Q100, bases open-source
Speeduino/rusEFI) — **não é o escopo deste guia**.

## 2. Protocolo — especificação byte a byte

Esta seção é a fonte de verdade. Implemente exatamente assim; se algo aqui
parecer estranho, ainda assim siga — foi desenhado pra combinar com o
parser que já existe e já está em produção no painel.

### 3.1 Telemetria (ECU → Painel), notify — já existe, protocolo fixo

Definido em `components/app_ecu/include/app_ecu.h` do repo do painel.

```
Byte 0:       0xEC                    marcador de início
Byte 1:       0x01                    versão do protocolo
Byte 2:       0x0E                    tamanho do payload (14, fixo pra v1)
Byte 3-4:     rpm            uint16 little-endian
Byte 5:       map_kpa        uint8
Byte 6:       tps_pct        uint8
Byte 7:       ect_c          int8   (signed, graus Celsius)
Byte 8:       iat_c          int8   (signed, graus Celsius)
Byte 9-10:    batt_mv        uint16 little-endian  (milivolts)
Byte 11-12:   lambda_x1000   uint16 little-endian  (1000 = estequiométrico)
Byte 13-16:   uptime_ms      uint32 little-endian  (ms desde o boot da ECU)
Byte 17:      checksum       XOR de todos os bytes 0..16
```

Tamanho total do frame: **18 bytes** — cabe num único BLE notify mesmo com
o MTU padrão (ATT_MTU 23 → 20 bytes úteis), não precisa fragmentar.

Envie este frame periodicamente (sugestão: a cada 100-300ms) pela
characteristic de notify de telemetria (UUID proposto em §4).

### 3.2 Mapas de tunagem (Painel → ECU, escrita) + status (ECU → Painel, notify)

Definido em `components/app_map/include/app_map.h` do repo do painel.
**Isto é uma exceção deliberada e revisada** à regra de "painel nunca
escreve na ECU" (documentada em `ROADMAP.md` §1 e §13 do repo do painel)
— o painel manda o mapa, mas **a ECU decide se aceita**.

Constantes:
```
APP_MAP_RPM_BINS   = 8    // pontos no eixo RPM
APP_MAP_LOAD_BINS  = 6    // pontos no eixo carga (kPa)
```

Três tabelas possíveis (mesmo eixo RPM x kPa, cada uma sua própria
transferência BEGIN/CHUNK/END — `table_id` abaixo):

Tabela de injeção (`table_id=0x00`): valores em **décimos de milissegundo**
(int16), ex.: `42` = 4.2ms. Faixa válida: `5` a `300` (0.5ms a 30.0ms) —
**qualquer valor fora disso deve ser clampado ou rejeitado pela ECU**,
nunca aplicado cru.

Tabela de ignição (`table_id=0x01`): valores em **décimos de grau** (int16,
com sinal), ex.: `150` = 15.0° (avanço), `-20` = -2.0° (retardo). Faixa
válida: `-100` a `600` (-10.0° a 60.0°) — mesma regra de clamping/rejeição.

Tabela de sonda (`table_id=0x02`): lambda **alvo** pra malha fechada
(closed-loop) — não é a leitura ao vivo da sonda, é o valor que a ECU
persegue em cada ponto de RPM/carga. Valores em **centésimos de lambda**
(int16), ex.: `85` = 0.85 (rico, plena carga/boost), `100` = 1.00
(estequiométrico). Faixa válida: `60` a `130` (lambda 0.60 a 1.30) — mesma
regra de clamping/rejeição.

Buffer serializado de UMA tabela (o que viaja fatiado nos pacotes
MAP_CHUNK abaixo), **nesta ordem exata**, tudo little-endian:
```
rpm_bins[8]        uint16 x 8  = 16 bytes   (eixo RPM, crescente)
load_kpa_bins[6]   uint16 x 6  = 12 bytes   (eixo carga/MAP, crescente)
celulas[6][8]      int16 x 48 = 96 bytes    (linha=carga, coluna=RPM)
                                = 124 bytes total (APP_MAP_SERIALIZED_LEN)
```

#### Pacotes Painel → ECU (characteristic de escrita), todos com esta forma:

```
Byte 0:      0xEA                marcador (comando de mapa)
Byte 1:      0x01                versão
Byte 2:      msg_type            0x01=BEGIN 0x02=CHUNK 0x03=END 0x04=ABORT
Byte 3..N-2: payload específico do tipo (ver abaixo)
Byte N-1:    checksum = XOR de TODOS os bytes anteriores DESTE pacote
```

**MAP_BEGIN** (9 bytes total):
```
[3]   table_id        0x00=injecao  0x01=ignicao  0x02=sonda
[4]   rpm_bins_count   (deve ser 8 — rejeite se vier diferente)
[5]   load_bins_count  (deve ser 6 — rejeite se vier diferente)
[6-7] total_len        uint16 LE — deve ser 124 (APP_MAP_SERIALIZED_LEN)
[8]   checksum
```

**MAP_CHUNK** (até 23 bytes total, dado variável até 16 bytes):
```
[3-4] seq        uint16 LE, começa em 0, incrementa 1 por pacote
[5]   len        quantos bytes de dado neste pacote (<=16)
[6..6+len-1]  dado — fatia sequencial do buffer serializado de 124 bytes
[6+len]  checksum
```
Vai chegar em `ceil(124/16) = 8` pacotes CHUNK (7 com 16 bytes + 1 com 12
bytes). Concatene os `dado` de cada CHUNK, na ordem de `seq`, num buffer de
124 bytes.

**MAP_END** (7 bytes total):
```
[3]   table_id   (deve bater com o do BEGIN — rejeite se não bater)
[4-5] crc16      uint16 LE — CRC-16/CCITT-FALSE do buffer de 124 bytes
                 reassemblado pelos CHUNK (ver algoritmo abaixo)
[6]   checksum
```
**Recalcule o CRC16 do buffer que você reassemblou e compare com este
campo. Se não bater, rejeite a tabela inteira (não aplique nada) e
responda `ERR_CRC` (ver status abaixo).**

**MAP_ABORT** (4 bytes: marcador+versão+tipo+checksum) — painel cancelou
uma transferência no meio. Descarte qualquer buffer parcial em andamento.

#### Algoritmo do CRC16 (CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF)

Implementação de referência (idêntica à usada no painel — teste com o
vetor abaixo antes de confiar no seu port):

```c
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
```

**Vetor de teste padrão (valide sua implementação com isto primeiro):**
`crc16_ccitt_false((uint8_t*)"123456789", 9)` deve dar **`0x29B1`**. Se não
der, tem bug no port antes mesmo de mexer com BLE — pare e corrija aqui
primeiro.

#### Pacote ECU → Painel (characteristic de notify DEDICADA — separada da
telemetria, não misture as duas):

```
Byte 0: 0xEB      marcador de status de mapa
Byte 1: 0x01      versão
Byte 2: status    ver tabela abaixo
Byte 3: table_id  (0=injecao, 1=ignicao, 2=sonda)
Byte 4: checksum = XOR dos bytes 0..3
```

| status | valor | quando mandar |
|---|---|---|
| ACK_BEGIN | 0x00 | recebeu MAP_BEGIN válido, pronto pros CHUNK |
| ACK_CHUNK | 0x01 | (opcional) confirma cada CHUNK — só implemente se quiser fluxo mais robusto; não é obrigatório |
| SAVED_OK | 0x02 | CRC bateu, validação passou, gravou na flash com sucesso |
| ERR_CRC | 0x03 | CRC16 do MAP_END não bateu com o buffer reassemblado |
| ERR_ENGINE_RUNNING | 0x04 | **recusa por segurança** — RPM simulado acima de zero no momento do MAP_END |
| ERR_OUT_OF_RANGE | 0x05 | algum valor da tabela está fora da faixa válida (§3.2) mesmo depois de tentar clampar |
| ERR_BUSY | 0x06 | já tem uma transferência em andamento (BEGIN chegou sem o anterior ter terminado) |

## 3. Regra de segurança — não é opcional

**A ECU deve recusar gravar um mapa novo se o "motor" (RPM simulado, neste
protótipo) estiver acima de zero.** Isso é uma decisão de arquitetura do
projeto todo (não só desta ECU): quem decide um mapa novo dentro de um
motor girando é o próprio time que fez o pedido, e a resposta técnica
segura é a ECU nunca aceitar escrita "ao vivo". Nesta bancada de teste,
"motor girando" = o potenciômetro de RPM (ver §5) lido acima de um
threshold pequeno (ex.: >50rpm simulado). Numa ECU real isso seria o sinal
de rotação de verdade.

Fluxo: ao receber MAP_END, ANTES de gravar na flash, cheque o RPM simulado
atual. Se > 0 (ou acima do threshold), responda `ERR_ENGINE_RUNNING` e
descarte o buffer — não grave nada.

## 4. UUIDs BLE propostos

O protocolo do painel (ROADMAP.md §4 do repo `lvgl_porting`) nunca definiu
UUIDs — ficou em aberto justamente esperando o firmware da ECU existir.
Seguem UUIDs propostos por esta sessão (são só identificadores, livre pra
gerar os seus com qualquer gerador de UUID v4 — só precisa manter os DOIS
lados, painel e ECU, sincronizados se trocar):

```
Serviço ECU (custom):              7a2b1000-ec00-4a5d-9f6b-1234567890ab
  Characteristic Telemetria (Notify):  7a2b1001-ec00-4a5d-9f6b-1234567890ab
  Characteristic Mapa Escrita (Write): 7a2b1002-ec00-4a5d-9f6b-1234567890ab
  Characteristic Mapa Status (Notify): 7a2b1003-ec00-4a5d-9f6b-1234567890ab
```

Anote esses UUIDs num lugar visível do seu projeto — quando o lado do
painel implementar o GATT client (ainda não implementado, é o próximo
passo depois deste guia), ele vai precisar destes mesmos valores.

## 5. Arquitetura de hardware — simulador por potenciômetro + multiplexador

O STM32WB5MM-DK é um kit compacto (módulo STM32WB5MMG + BLE integrado,
OLED, botões touch, 2 microfones digitais, LED RGB) — **não tem quantidade
grande de pinos livres** nos conectores de expansão, ao contrário de uma
Nucleo com headers Arduino. Por isso, em vez de 7 pinos ADC dedicados (um
por sensor), a recomendação é usar **um multiplexador analógico CD4051**
(8 canais, ~R$3-5, muito comum) — assim você usa só **1 pino ADC + 3
pinos digitais de seleção** pra ler até 8 potenciômetros.

```
                         STM32WB5MM-DK
                    ┌───────────────────────┐
                    │                       │
   Pot RPM  ───┐    │                       │
   Pot MAP  ───┤    │                       │
   Pot TPS  ───┤    │                       │
   Pot ECT  ───┼──► CD4051 ── COM ─────────►│ ADC1_IN (1 pino)
   Pot IAT  ───┤    (mux 8:1)               │
   Pot BATT ───┤       │  │  │              │
   Pot LAMBDA──┘       A  B  C              │
   (canal 7 livre)     │  │  │              │
                        └──┴──┴───────────► 3x GPIO saída (S0,S1,S2)
                    │                       │
                    │   [potenciômetro de   │
                    │    RPM tem threshold  │
                    │    "motor parado" em  │
                    │    zero — ver §3]     │
                    └───────────────────────┘

Cada potenciômetro: terminais externos em 3V3 e GND (do próprio board),
cursor (wiper) no canal correspondente do CD4051.
CD4051: VDD=3V3, VSS/VEE=GND, INH=GND (sempre habilitado).
```

**Pinos exatos:** não estou assumindo quais GPIOs estão fisicamente livres
neste board específico — o jeito CORRETO e seguro de descobrir isso é:

1. Abra o STM32CubeMX (ou o wizard de novo projeto do STM32CubeIDE).
2. Em "Board Selector" (não "MCU Selector"), procure **"STM32WB5MM-DK"** e
   selecione a placa exata (não só o chip). Isso faz o CubeMX marcar
   automaticamente todos os pinos já usados pelo OLED, botões touch, LED
   RGB e microfones como reservados/ocupados.
3. Os pinos que sobrarem livres no(s) conector(es) de expansão são os que
   você pode usar. Anote quais são (variam conforme a revisão do board) e
   use esse mapeamento real no lugar dos nomes genéricos deste guia.
4. Confirme contra o **UM2434** (User Manual oficial do STM32WB5MM-DK) antes
   de soldar ou ligar qualquer fio — ele tem a tabela de pinos do
   conector e o desenho de posição física.

Se sobrar folga suficiente de pinos ADC (confirmando no passo 2 acima),
dá pra simplificar e ligar os potenciômetros direto, sem o CD4051 — só
funciona se o board tiver 7 canais ADC livres nos conectores, o que
depende da revisão física. O mux é a opção que funciona independente
disso.

**UART de debug:** o STM32WB5MM-DK inclui um ST-LINK integrado (na parte
do board dedicada à programação/debug) que normalmente já expõe uma porta
serial virtual (Virtual COM Port) pelo mesmo cabo USB usado pra gravar o
firmware — geralmente ligada ao USART1 do MCU, mas **confirme no UM2434**
qual USART é essa no seu board. Use essa UART já pronta pro debug em vez
de dedicar outro pino/periférico só pra isso.

## 6. Passo a passo — STM32CubeIDE / STM32CubeMX

Siga nesta ordem, testando a cada etapa antes de ir pra próxima (não
escreva o firmware inteiro de uma vez):

1. **Novo projeto STM32**: File → New → STM32 Project → aba "Board
   Selector" → busque `STM32WB5MM-DK` → Next → dê um nome (ex.
   `ecu_simulador_ble`) → Finish. Deixe o CubeMX inicializar os
   periféricos padrão do board quando perguntar.
2. **Confirme os pinos livres** (ver §5, passo 2-3) antes de continuar.
3. **Configurar ADC1**: modo "Single-ended", canal ligado ao pino do
   COM do CD4051, resolução 12 bits, sem DMA por enquanto (polling é
   suficiente pra 8 canais lidos a cada ~100ms).
4. **Configurar 3 GPIO de saída** pros pinos S0/S1/S2 do CD4051 (modo
   Output Push-Pull, sem pull, velocidade baixa — não tem nenhuma pressa
   nesse sinal).
5. **Configurar USART** de debug (a que já vem ligada ao ST-LINK VCP, ver
   §5) — 115200 8N1, modo assíncrono, sem DMA por enquanto.
6. **Configurar BLE (middleware "Bluetooth_LE")**: habilite a stack BLE,
   crie um Serviço custom com o UUID de §4, e dentro dele 3
   Characteristics:
   - Telemetria: propriedade **Notify**, tamanho fixo 18 bytes.
   - Mapa Escrita: propriedade **Write** (ou Write Without Response, mais
     rápido — mas aí você perde a confirmação de entrega no nível BLE;
     comece com Write simples), tamanho máximo 23 bytes.
   - Mapa Status: propriedade **Notify**, tamanho fixo 5 bytes.
   O CubeMX gera `custom_stm.h/.c` com enums e callbacks fracos
   (`Custom_STM_App_Notification` ou nome equivalente, o nome exato varia
   um pouco por versão do CubeMX/CubeWB — use o que ele gerar no SEU
   projeto, não invente um nome diferente).
7. **Gerar código** (Project → Generate Code).
8. A partir daqui, o código de aplicação vai em `Core/Src/app_*.c` ou
   `STM32_WPAN/App/*` (conforme o template do CubeWB) — **não edite os
   arquivos gerados automaticamente pelo CubeMX** (`custom_stm.c`,
   `ble_*.c` no topo) fora das seções `USER CODE BEGIN/END`, senão o
   próximo "Generate Code" apaga suas mudanças.

## 7. Estrutura de código sugerida

```
app_ecu_sim.c/.h     — leitura dos potenciômetros (mux), conversão pra
                        unidade de engenharia, guarda o "snapshot" atual
app_ecu_protocol.c/.h — monta o frame de telemetria (§3.1), monta/decodifica
                        os pacotes de mapa (§3.2), calcula o CRC16
app_map_storage.c/.h — grava/lê o mapa na flash interna (ver §8.4)
app_debug_uart.c/.h  — imprime tudo em texto legível na UART
main.c                — laço principal: le sensores a cada Xms, manda
                        telemetria, processa mapas recebidos (via callback
                        do CubeMX), decide validação de segurança
```

## 8. Código de referência

### 8.1 Leitura dos sensores (mux + ADC + escala)

```c
typedef enum {
    SENS_RPM = 0, SENS_MAP, SENS_TPS, SENS_ECT,
    SENS_IAT, SENS_BATT, SENS_LAMBDA, SENS_COUNT
} sensor_ch_t;

static void mux_select(uint8_t channel /* 0-7 */)
{
    HAL_GPIO_WritePin(MUX_S0_GPIO_Port, MUX_S0_Pin, (channel & 0x01) ? SET : RESET);
    HAL_GPIO_WritePin(MUX_S1_GPIO_Port, MUX_S1_Pin, (channel & 0x02) ? SET : RESET);
    HAL_GPIO_WritePin(MUX_S2_GPIO_Port, MUX_S2_Pin, (channel & 0x04) ? SET : RESET);
}

static uint16_t read_adc_raw(uint8_t channel)
{
    mux_select(channel);
    HAL_Delay(1);  // acomoda o mux antes de amostrar
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;  // 0-4095 (12 bits)
}

// Ajuste as faixas conforme quiser — isto e so um simulador de bancada.
typedef struct { uint16_t rpm; uint8_t map_kpa, tps_pct; int8_t ect_c, iat_c;
                 uint16_t batt_mv; uint16_t lambda_x1000; } ecu_sim_data_t;

static void read_all_sensors(ecu_sim_data_t *out)
{
    uint16_t raw[SENS_COUNT];
    for (int i = 0; i < SENS_COUNT; i++) raw[i] = read_adc_raw(i);

    out->rpm          = (uint16_t)((uint32_t)raw[SENS_RPM] * 8000u / 4095u);
    out->map_kpa       = (uint8_t)(20 + (uint32_t)raw[SENS_MAP] * 85u / 4095u);
    out->tps_pct        = (uint8_t)((uint32_t)raw[SENS_TPS] * 100u / 4095u);
    out->ect_c          = (int8_t)(-20 + (int32_t)raw[SENS_ECT] * 140 / 4095);
    out->iat_c          = (int8_t)(-20 + (int32_t)raw[SENS_IAT] * 100 / 4095);
    out->batt_mv        = (uint16_t)(8000u + (uint32_t)raw[SENS_BATT] * 8000u / 4095u);
    out->lambda_x1000   = (uint16_t)(700u + (uint32_t)raw[SENS_LAMBDA] * 600u / 4095u);
}

// Usado pela trava de seguranca do §3: motor "girando" = RPM simulado > threshold.
static bool engine_is_running(const ecu_sim_data_t *d) { return d->rpm > 50; }
```

### 8.2 Montar e enviar o frame de telemetria

```c
static uint8_t s_uptime_frame[18];

static void build_telemetry_frame(const ecu_sim_data_t *d, uint32_t uptime_ms, uint8_t *out)
{
    out[0] = 0xEC; out[1] = 0x01; out[2] = 14;
    out[3] = (uint8_t)(d->rpm & 0xFF);        out[4] = (uint8_t)(d->rpm >> 8);
    out[5] = d->map_kpa;
    out[6] = d->tps_pct;
    out[7] = (uint8_t)d->ect_c;
    out[8] = (uint8_t)d->iat_c;
    out[9] = (uint8_t)(d->batt_mv & 0xFF);     out[10] = (uint8_t)(d->batt_mv >> 8);
    out[11] = (uint8_t)(d->lambda_x1000 & 0xFF); out[12] = (uint8_t)(d->lambda_x1000 >> 8);
    out[13] = (uint8_t)(uptime_ms & 0xFF);
    out[14] = (uint8_t)((uptime_ms >> 8) & 0xFF);
    out[15] = (uint8_t)((uptime_ms >> 16) & 0xFF);
    out[16] = (uint8_t)((uptime_ms >> 24) & 0xFF);

    uint8_t chk = 0;
    for (int i = 0; i < 17; i++) chk ^= out[i];
    out[17] = chk;
}

// Chame isto periodicamente (ex.: a cada 150-300ms) no laco principal.
// "Custom_STM_UpdateChar" (ou nome equivalente) e a funcao que o CubeMX
// gera pra atualizar/notificar uma characteristic — use a que ele gerou
// no SEU projeto pra characteristic de Telemetria (UUID de §4).
static void send_telemetry(void)
{
    ecu_sim_data_t d;
    read_all_sensors(&d);
    build_telemetry_frame(&d, HAL_GetTick(), s_uptime_frame);

    // Custom_STM_UpdateChar(CUSTOM_STM_TELEMETRIA, s_uptime_frame); // <- ajuste ao nome gerado

    debug_print_telemetry(&d);  // ver 8.5
}
```

### 8.3 Recepção do mapa — máquina de estados BEGIN/CHUNK/END

```c
typedef struct {
    bool     in_progress;
    uint8_t  table_id;
    uint16_t total_len;
    uint8_t  buf[124];      // APP_MAP_SERIALIZED_LEN
    uint16_t received_len;
    uint16_t expected_seq;
} map_rx_state_t;

static map_rx_state_t s_rx = {0};

static void send_map_status(uint8_t status, uint8_t table_id)
{
    uint8_t pkt[5];
    pkt[0] = 0xEB; pkt[1] = 0x01; pkt[2] = status; pkt[3] = table_id;
    pkt[4] = pkt[0] ^ pkt[1] ^ pkt[2] ^ pkt[3];
    // Custom_STM_UpdateChar(CUSTOM_STM_MAPA_STATUS, pkt); // <- ajuste ao nome gerado
}

// Chame isto de dentro do callback que o CubeMX gerar quando a
// characteristic de escrita de mapa (§4) receber dados.
void on_map_write_received(const uint8_t *data, uint16_t len)
{
    if (len < 4) return;  // pacote curto demais pra ter checksum valido
    uint8_t chk = 0;
    for (int i = 0; i < len - 1; i++) chk ^= data[i];
    if (chk != data[len - 1]) return;  // checksum do PACOTE nao bateu — descarta silenciosamente

    uint8_t msg_type = data[2];

    if (msg_type == 0x01) {  // MAP_BEGIN
        if (data[4] != 8 || data[5] != 6) { send_map_status(0x05, data[3]); return; } // ERR_OUT_OF_RANGE
        uint16_t total_len = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
        if (total_len != 124) { send_map_status(0x05, data[3]); return; }

        s_rx.in_progress   = true;
        s_rx.table_id      = data[3];
        s_rx.total_len     = total_len;
        s_rx.received_len  = 0;
        s_rx.expected_seq  = 0;
        send_map_status(0x00, data[3]);  // ACK_BEGIN

    } else if (msg_type == 0x02) {  // MAP_CHUNK
        if (!s_rx.in_progress) { send_map_status(0x06, data[3]); return; } // ERR_BUSY (nao tinha BEGIN)
        uint16_t seq = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
        uint8_t  chunk_len = data[5];
        if (seq != s_rx.expected_seq || s_rx.received_len + chunk_len > sizeof(s_rx.buf)) {
            s_rx.in_progress = false;  // sequencia quebrada — aborta
            return;
        }
        memcpy(&s_rx.buf[s_rx.received_len], &data[6], chunk_len);
        s_rx.received_len += chunk_len;
        s_rx.expected_seq++;

    } else if (msg_type == 0x03) {  // MAP_END
        if (!s_rx.in_progress || data[3] != s_rx.table_id || s_rx.received_len != s_rx.total_len) {
            send_map_status(0x03, data[3]); // ERR_CRC (dado incompleto/inconsistente)
            s_rx.in_progress = false;
            return;
        }
        uint16_t crc_recv = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
        uint16_t crc_calc = crc16_ccitt_false(s_rx.buf, s_rx.received_len);
        s_rx.in_progress = false;

        if (crc_recv != crc_calc) { send_map_status(0x03, data[3]); return; } // ERR_CRC

        ecu_sim_data_t cur;
        read_all_sensors(&cur);
        if (engine_is_running(&cur)) {
            send_map_status(0x04, data[3]);  // ERR_ENGINE_RUNNING — nao grava nada
            return;
        }

        if (!validate_and_clamp_table(s_rx.buf, data[3])) {
            send_map_status(0x05, data[3]);  // ERR_OUT_OF_RANGE
            return;
        }

        if (map_storage_save(data[3], s_rx.buf, s_rx.received_len)) {
            send_map_status(0x02, data[3]);  // SAVED_OK
        } else {
            send_map_status(0x06, data[3]);  // ERR_BUSY (falha de flash — reaproveitado)
        }

    } else if (msg_type == 0x04) {  // MAP_ABORT
        s_rx.in_progress = false;
    }
}

// Confere/clampa cada celula contra as faixas do §3.2 (offsets 28..123 do
// buffer = celulas; os primeiros 28 bytes sao os eixos, que tambem valeria
// a pena validar como crescentes, mas isso fica a seu criterio).
static bool validate_and_clamp_table(uint8_t *buf, uint8_t table_id)
{
    int16_t lo, hi;
    switch (table_id) {
        case 0:  lo = 5;    hi = 300; break;   // injecao: 0.5-30.0ms (decimos de ms)
        case 1:  lo = -100; hi = 600; break;   // ignicao: -10.0 a 60.0 graus (decimos de grau)
        case 2:  lo = 60;   hi = 130; break;   // sonda: lambda 0.60-1.30 (centesimos)
        default: return false;                 // table_id desconhecido — rejeita
    }
    for (int i = 28; i + 1 < 124; i += 2) {
        int16_t v = (int16_t)((uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8));
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        buf[i] = (uint8_t)(v & 0xFF);
        buf[i + 1] = (uint8_t)((v >> 8) & 0xFF);
    }
    return true;  // troque por "return false" se preferir REJEITAR em vez de clampar
}
```

### 8.4 Gravação na flash interna — cuidado com a partição da pilha BLE

O STM32WB55 é dual-core: o CPU1 (Cortex-M4, onde seu código roda) e o CPU2
(Cortex-M0+, roda a pilha BLE) **dividem a mesma flash física**. A pilha
BLE ocupa uma faixa de endereços no topo da flash que **NÃO PODE** ser
sobrescrita pelo seu código, senão a stack BLE para de funcionar
(brick parcial, precisa regravar a pilha via STM32CubeProgrammer).

**Antes de escolher o endereço de gravação:**
1. Abra o STM32CubeProgrammer, conecte no board, e olhe o mapa de memória
   (ele mostra onde termina a área do CPU1/aplicação e onde começa a
   reservada pra pilha wireless — isso muda conforme a versão da pilha
   BLE instalada, "FUS"/"stack BLE" na aba correspondente).
2. Escolha UMA página de flash (4KB no STM32WB55) que sobre livre **antes**
   dessa fronteira — não perto do limite superior do seu próprio código
   de aplicação nem da área reservada.
3. Use esse endereço real no lugar de `MAP_FLASH_PAGE_ADDR` abaixo.

```c
// AJUSTE este endereço conforme o que o CubeProgrammer mostrar no SEU
// board/versao de pilha BLE — nao use um valor de outro projeto sem
// confirmar. Exemplo de estrutura (nao o endereco em si):
#define MAP_FLASH_PAGE_ADDR   0x0803F000UL  // <-- CONFIRME antes de usar
#define MAP_FLASH_PAGE_NUM    ((MAP_FLASH_PAGE_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE)

// Layout simples: [magic u32][table_id u8][pad][len u16][buf 124][crc16]
static bool map_storage_save(uint8_t table_id, const uint8_t *buf, uint16_t len)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Page      = MAP_FLASH_PAGE_NUM + table_id;  // uma pagina por tabela
    erase.NbPages   = 1;
    uint32_t err_page;
    if (HAL_FLASHEx_Erase(&erase, &err_page) != HAL_OK) { HAL_FLASH_Lock(); return false; }

    uint32_t addr = MAP_FLASH_PAGE_ADDR + (uint32_t)table_id * FLASH_PAGE_SIZE;
    uint32_t magic = 0x50414D45; // "EMAP" reverso, so um marcador de "valido"

    bool ok = true;
    ok &= HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, ((uint64_t)table_id << 32) | magic) == HAL_OK;
    addr += 8;
    for (uint16_t i = 0; i < len; i += 8) {
        uint64_t word = 0;
        memcpy(&word, &buf[i], (len - i >= 8) ? 8 : (len - i));
        ok &= HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, word) == HAL_OK;
        addr += 8;
    }

    HAL_FLASH_Lock();
    return ok;
}

// Chame isto no boot, ANTES de iniciar o BLE — se existir mapa salvo,
// carrega pra RAM; senao, mantem os valores padrao/zerados.
static bool map_storage_load(uint8_t table_id, uint8_t *out_buf, uint16_t len)
{
    uint32_t addr = MAP_FLASH_PAGE_ADDR + (uint32_t)table_id * FLASH_PAGE_SIZE;
    uint32_t magic = *(volatile uint32_t *)addr;
    if (magic != 0x50414D45) return false;  // nada gravado ainda
    memcpy(out_buf, (const void *)(addr + 8), len);
    return true;
}
```

### 8.5 Debug via UART (texto legível)

```c
static void debug_print_telemetry(const ecu_sim_data_t *d)
{
    char line[128];
    int n = snprintf(line, sizeof(line),
        "[TX] RPM=%u MAP=%ukPa TPS=%u%% ECT=%dC IAT=%dC BATT=%umV LAMBDA=%u\r\n",
        d->rpm, d->map_kpa, d->tps_pct, d->ect_c, d->iat_c, d->batt_mv, d->lambda_x1000);
    HAL_UART_Transmit(&huart1, (uint8_t *)line, n, 100);
}

static void debug_print_map_status(uint8_t status, uint8_t table_id)
{
    static const char *names[] = {"ACK_BEGIN","ACK_CHUNK","SAVED_OK",
                                   "ERR_CRC","ERR_ENGINE_RUNNING","ERR_OUT_OF_RANGE","ERR_BUSY"};
    char line[80];
    int n = snprintf(line, sizeof(line), "[MAP] table=%u status=%s\r\n",
                      table_id, (status < 7) ? names[status] : "?");
    HAL_UART_Transmit(&huart1, (uint8_t *)line, n, 100);
}
```
Chame `debug_print_map_status` de dentro de `send_map_status` (ou logo
antes/depois de cada chamada dela) pra ver no terminal serial exatamente
o que a ECU decidiu e por quê.

## 9. Passo a passo de teste/validação

1. **Sem o painel ainda**: grave o firmware, abra um terminal serial
   (PuTTY/minicom/screen, 115200 8N1) na porta COM do ST-LINK VCP. Gire os
   potenciômetros e confira se as linhas `[TX] RPM=... MAP=...` mudam de
   forma plausível.
2. **Confira o CRC16** isoladamente antes de testar BLE de verdade — teste
   unitário simples chamando `crc16_ccitt_false` com o vetor `"123456789"`
   e conferindo `0x29B1` (ver §3.2). Não prossiga pro BLE sem isso bater.
3. **Teste BLE com um app genérico primeiro** (nRF Connect, LightBlue,
   etc., no celular) antes de tentar conectar com o painel de verdade —
   confirme que o serviço/characteristics aparecem com os UUIDs certos
   (§4), que a Telemetria notifica 18 bytes periodicamente, e que dá pra
   escrever manualmente um pacote MAP_BEGIN de teste na characteristic de
   escrita e ver a resposta de status aparecer na de notify.
4. **Teste a trava de segurança**: com o potenciômetro de RPM girado
   (>0), tente mandar um mapa — confirme que a ECU responde
   `ERR_ENGINE_RUNNING` e **não** grava (releia da flash depois pra
   confirmar que o valor antigo continua lá).
5. **Só depois disso**, conecte de verdade com o painel — o lado do
   painel ainda precisa do GATT client implementado (não existe ainda,
   ver §10) pra consumir isso automaticamente; até lá, os passos 1-4 acima
   já validam a ECU sozinha.

## 10. O que falta do lado do painel (fora do escopo deste guia)

Este guia cobre só o firmware da ECU. Pro painel realmente CONSUMIR essa
telemetria e mandar mapas de verdade, falta implementar (no repo
`lvgl_porting`, componente `app_ble`):
- `ble_gattc_disc_svc_by_uuid` → descobrir o serviço pelo UUID de §4.
- `ble_gattc_disc_all_chrs` → descobrir as characteristics.
- `ble_gattc_subscribe` na characteristic de Telemetria → encaminhar bytes
  recebidos pra `app_ecu_feed_ble_notify()` (já existe e já funciona).
- `ble_gattc_subscribe` na characteristic de Status de Mapa → encaminhar
  pra um novo `app_map_feed_status_notify()` (ainda não existe).
- `ble_gattc_write` pra characteristic de Mapa Escrita → é o que
  `app_map_send_to_ecu()` vai chamar de verdade (hoje é um stub, retorna
  `ESP_ERR_NOT_SUPPORTED`).

Isso é trabalho separado, do lado ESP32 — depois que a ECU (este guia)
estiver funcionando e você confirmar os UUIDs/comportamento reais com um
app de BLE genérico, essa é a próxima peça a pedir.

## 11. Limitações conhecidas deste simulador

- Não lê motor real nenhum — é só potenciômetro simulando valor.
- Sem autenticação/pareamento BLE (par de segurança) — qualquer
  dispositivo próximo pode se conectar. Aceitável pra bancada de teste;
  **não é adequado pra um produto final** sem revisar isso (bonding/pairing
  do NimBLE/STM32WB, fora do escopo aqui).
- `validate_and_clamp_table` (§8.3) clampa em vez de rejeitar por padrão —
  troque pra rejeitar (`return false`) se preferir uma política mais
  estrita (a diferença é: clampar aceita o mapa mas força os valores pra
  dentro da faixa seguro; rejeitar devolve `ERR_OUT_OF_RANGE` e não muda
  nada). Isso é uma escolha de produto, não uma resposta técnica única.
- O `ACK_CHUNK` do protocolo é opcional — sem ele, uma transferência que
  perder um pacote no meio só vai ser detectada no MAP_END (CRC não bate).
  Pra bancada de teste isso é aceitável; pra um produto final, considere
  implementar confirmação por chunk.
