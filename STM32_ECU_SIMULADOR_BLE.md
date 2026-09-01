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

### 3.2 Mapas de tunagem (Painel → ECU) — protocolo UDS (ISO 14229-1)

Definido em `components/app_map/include/app_map.h` do repo do painel.
**Isto é uma exceção deliberada e revisada** à regra de "painel nunca
escreve na ECU" (documentada em `ROADMAP.md` §1 e §13 do repo do painel)
— o painel manda o mapa, mas **a ECU decide se aceita**.

**Este NÃO é um protocolo inventado do zero.** É baseado no **UDS —
Unified Diagnostic Services (ISO 14229-1)**, o protocolo automotivo real
usado por ferramentas de concessionária/oficina pra gravar calibração
numa ECU. Os Service IDs, subfunções e códigos de erro abaixo são os REAIS
do padrão — pesquisados e confirmados, não inventados (ver fontes no
final desta seção). A única parte que não é "oficial" é o transporte:
UDS normalmente roda sobre CAN (com fragmentação ISO-TP, ISO 15765-2) ou
sobre Ethernet (DoIP); aqui adaptamos pra BLE GATT, que não precisa da
fragmentação do ISO-TP porque cada PDU já é dimensionado pra caber num
único pacote BLE.

**Por que UDS e não XCP** (o outro protocolo automotivo de calibração,
ASAM MCD-1 XCP): XCP foi feito pra tunagem AO VIVO, motor rodando
(measure-and-adjust em tempo real). Este projeto decidiu o oposto — só
aceita gravar um mapa novo com o motor PARADO (ver §3 abaixo). Isso é
exatamente o caso de uso do fluxo RequestDownload/TransferData/
RequestTransferExit do UDS: reflash de calibração em "modo de serviço",
não ajuste em tempo real. A escolha de protocolo segue a decisão de
segurança já tomada, não o contrário.

Constantes:
```
APP_MAP_RPM_BINS   = 8    // pontos no eixo RPM
APP_MAP_LOAD_BINS  = 6    // pontos no eixo carga (kPa)
```

Três tabelas possíveis (mesmo eixo RPM x kPa, cada uma sua própria
sequência de transferência — endereço lógico de 16 bits, não é endereço
de flash real, ver `RequestDownload` abaixo):

| Tabela | Endereço lógico | Unidade | Faixa válida |
|---|---|---|---|
| Injeção | `0x0000` | décimos de ms (ex.: `42`=4.2ms) | `5` a `300` (0.5–30.0ms) |
| Ignição | `0x0001` | décimos de grau, com sinal (ex.: `150`=15.0°, `-20`=-2.0°) | `-100` a `600` (-10.0° a 60.0°) |
| Sonda (lambda alvo) | `0x0002` | centésimos de lambda (ex.: `85`=0.85 rico, `100`=1.00 estequiométrico) | `60` a `130` (0.60–1.30) |

**Qualquer valor fora da faixa deve ser clampado ou rejeitado pela ECU,
nunca aplicado cru.**

Buffer serializado de UMA tabela (o que viaja fatiado nos `TransferData`
abaixo), **nesta ordem exata**, tudo little-endian:
```
rpm_bins[8]        uint16 x 8  = 16 bytes   (eixo RPM, crescente)
load_kpa_bins[6]   uint16 x 6  = 12 bytes   (eixo carga/MAP, crescente)
celulas[6][8]      int16 x 48 = 96 bytes    (linha=carga, coluna=RPM)
                                = 124 bytes total (APP_MAP_SERIALIZED_LEN)
```

#### Sequência completa (painel = "tester", ECU = "server" — nomenclatura
do próprio padrão)

Cada PDU abaixo é UM write ou UM notify BLE só (sem fragmentação — já
dimensionamos cada um pra caber). Resposta positiva = SID pedido + `0x40`
(ex.: `0x34` responde `0x74`). Resposta negativa = **sempre** `0x7F`
seguido do SID original e um código de erro (NRC) — ver tabela de NRCs
mais abaixo.

**1) DiagnosticSessionControl — entra em modo de gravação**
```
Painel → ECU:  [0x10] [0x02]              (0x02 = programmingSession)
ECU → Painel:  [0x50] [0x02] [...]        (positiva) OU
               [0x7F] [0x10] [NRC]        (negativa)
```
⚠️ **A ECU DEVE recusar aqui** (NRC `0x22 conditionsNotCorrect`) **se o
motor estiver girando.** Este é o primeiro e principal ponto de recusa —
ver §3.

**2) SecurityAccess — seed/key (nível 1)**
```
Painel → ECU:  [0x27] [0x01]                       (pede seed)
ECU → Painel:  [0x67] [0x01] [seed_lo] [seed_hi]    (16 bits, gerado pela ECU)

Painel → ECU:  [0x27] [0x02] [key_lo] [key_hi]      (key calculada do seed)
ECU → Painel:  [0x67] [0x02]                        (positiva: acesso liberado) OU
               [0x7F] [0x27] [NRC]                  (0x35 invalidKey, 0x33 securityAccessDenied)
```
⚠️ **O algoritmo seed→key abaixo é uma transformação simples DE
DEMONSTRAÇÃO, não é criptografia real.** O objetivo é impedir que
qualquer app BLE genérico escreva um mapa por acidente/curiosidade — não
resistir a um atacante que capturou o tráfego BLE. Troque por algo mais
forte antes de qualquer uso além de bancada.
```c
// Mesmo algoritmo dos dois lados — se um lado mudar sem o outro, toda
// escrita de mapa passa a falhar com invalidKey.
uint16_t compute_key(uint16_t seed)
{
    uint16_t rotated = (uint16_t)((seed << 3) | (seed >> 13));
    return (uint16_t)(rotated ^ 0xA5A5);
}
```

**3) RequestDownload — declara qual tabela e o tamanho**
```
Painel → ECU:  [0x34] [0x00] [0x22] [addr_lo] [addr_hi] [size_lo] [size_hi]
               dataFormatIdentifier=0x00 (sem compressão)
               addressAndLengthFormatIdentifier=0x22 (2 bytes endereço + 2 bytes tamanho)
               addr = endereço lógico da tabela (ver tabela acima)
               size = sempre 124 (APP_MAP_SERIALIZED_LEN)

ECU → Painel:  [0x74] [lengthFormatId] [maxBlockLen...]   (positiva) OU
               [0x7F] [0x34] [NRC]                         (0x22, 0x31 requestOutOfRange, 0x70)
```
Use `lengthFormatId=0x20` (1 byte segue) e declare `maxBlockLen=16` — é
quanto dado (sem contar o cabeçalho SID+BSC) cabe em cada `TransferData`
com folga no MTU padrão de BLE.

**4) TransferData — repetido até cobrir os 124 bytes**
```
Painel → ECU:  [0x36] [BSC] [até 16 bytes de dado]
ECU → Painel:  [0x76] [BSC]                (positiva, ecoa o BSC) OU
               [0x7F] [0x36] [NRC]         (0x73 wrongBlockSequenceCounter, 0x24 requestSequenceError)
```
`BSC` (blockSequenceCounter) é 1 byte, começa em `0x01`, incrementa a cada
pacote (`ceil(124/16) = 8` pacotes: 7 de 16 bytes + 1 de 12). **Regra
importante do padrão:** se o painel reenviar o MESMO BSC do pacote
anterior (porque não recebeu a resposta a tempo), isso é válido — aceite
de novo, não é erro. Só um BSC fora de sequência de verdade é erro.

**5) RequestTransferExit — fecha e valida**
```
Painel → ECU:  [0x37] [crc16_lo] [crc16_hi]
ECU → Painel:  [0x77] [0x00]               (positiva: gravou com sucesso) OU
               [0x7F] [0x37] [NRC]         (0x72 generalProgrammingFailure = CRC não bateu,
                                             0x22 conditionsNotCorrect = motor ligou no meio)
```
O CRC16 vai no campo `transferRequestParameterRecord` (o padrão deixa
esse campo livre pro fabricante — usamos pra validação de integridade,
igual a como muitas ECUs reais fazem). **Recalcule o CRC do buffer que
você reassemblou e compare. Se não bater, não grave nada e responda
`0x72`.** Antes de gravar, **recheque o motor parado de novo** (pode ter
ligado durante a transferência) — se estiver girando, responda `0x22` em
vez de `0x72`.

#### Códigos de erro (NRC) usados nesta troca

| NRC | Valor | Quando responder |
|---|---|---|
| conditionsNotCorrect | `0x22` | motor girando (na sessão ou no TransferExit) |
| requestSequenceError | `0x24` | serviço fora de ordem (ex.: TransferData sem RequestDownload antes) |
| requestOutOfRange | `0x31` | endereço de tabela inválido, tamanho ≠ 124, célula fora da faixa mesmo após tentar clampar |
| securityAccessDenied | `0x33` | tentou RequestDownload sem completar o SecurityAccess antes |
| invalidKey | `0x35` | key enviada não bate com a calculada a partir do seed |
| generalProgrammingFailure | `0x72` | CRC16 do TransferExit não bateu, ou falha ao gravar na flash |
| wrongBlockSequenceCounter | `0x73` | BSC fora de ordem (não é repetição do anterior) |
| subFunctionNotSupportedInActiveSession | `0x7E` | tentou RequestDownload/TransferData ainda na sessão default (esqueceu o passo 1) |

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

**Fontes consultadas pra esta seção** (pesquisado nesta sessão, não é só
memória do modelo):
- [UDS protocol (ISO 14229) explained: a guide to the services — Diadrom Insights](https://diadrom.com/insights/uds-iso-14229-explained/)
- [Request Download (0x34) Service: UDS Protocol — PiEmbSysTech](https://piembsystech.com/request-download-0x34-service-uds-protocol/)
- [Negative Response Codes (NRC): UDS Protocol — PiEmbSysTech](https://piembsystech.com/negative-response-codes-nrc-uds-protocol/)
- [UDS NRC Codes: Your Guide to Automotive Diagnostics — rfwireless-world](https://www.rfwireless-world.com/terminology/uds-nrc-codes)
- [Security Access Service Identifier (0x27): UDS Protocol — PiEmbSysTech](https://piembsystech.com/security-access-service-identifier-0x27-uds-protocol/)
- [UDS Security Access — Shayan Mukhtar](https://shayanmukhtar.com/2021/04/24/uds-security-access/)
- [ASAM MCD-1 XCP — Wiki oficial ASAM](https://www.asam.net/standards/detail/mcd-1-xcp/wiki/)
- [Understanding CCP and XCP: reading and tuning an ECU — Influx Technology](https://influxtechnology.com/blogs/learn/understanding-ccp-xcp)

## 3. Regra de segurança — não é opcional

**A ECU deve recusar gravar um mapa novo se o "motor" (RPM simulado, neste
protótipo) estiver acima de zero.** Isso é uma decisão de arquitetura do
projeto todo (não só desta ECU): quem decide um mapa novo dentro de um
motor girando é o próprio time que fez o pedido, e a resposta técnica
segura é a ECU nunca aceitar escrita "ao vivo". Nesta bancada de teste,
"motor girando" = o potenciômetro de RPM (ver §5) lido acima de um
threshold pequeno (ex.: >50rpm simulado). Numa ECU real isso seria o sinal
de rotação de verdade.

Dois pontos de checagem (defesa em profundidade, igual ao padrão UDS
real): ao receber `DiagnosticSessionControl(programmingSession)` — recusa
principal, antes de gastar tempo com segurança/transferência — e de novo
no `RequestTransferExit`, caso o motor tenha ligado durante a
transferência. Em qualquer um dos dois, responda `0x22
conditionsNotCorrect` e não grave nada.

## 4. UUIDs BLE propostos

O protocolo do painel (ROADMAP.md §4 do repo `lvgl_porting`) nunca definiu
UUIDs — ficou em aberto justamente esperando o firmware da ECU existir.
Seguem UUIDs propostos por esta sessão (são só identificadores, livre pra
gerar os seus com qualquer gerador de UUID v4 — só precisa manter os DOIS
lados, painel e ECU, sincronizados se trocar). Note que agora só precisa
de UM par escrita/notify pra TODOS os serviços UDS (sessão, segurança,
download, transfer, exit) — é assim que o UDS real funciona também, um
canal só multiplexado pelo SID no primeiro byte de cada PDU:

```
Serviço ECU (custom):                 7a2b1000-ec00-4a5d-9f6b-1234567890ab
  Characteristic Telemetria (Notify):    7a2b1001-ec00-4a5d-9f6b-1234567890ab
  Characteristic UDS Request (Write):    7a2b1002-ec00-4a5d-9f6b-1234567890ab
  Characteristic UDS Response (Notify):  7a2b1003-ec00-4a5d-9f6b-1234567890ab
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
   - UDS Request: propriedade **Write** (ou Write Without Response, mais
     rápido — mas aí você perde a confirmação de entrega no nível BLE;
     comece com Write simples), tamanho máximo 18 bytes — recebe QUALQUER
     PDU da sequência do §3.2 (sessão, seed/key, RequestDownload,
     TransferData, RequestTransferExit), diferenciados pelo SID (primeiro
     byte).
   - UDS Response: propriedade **Notify**, tamanho máximo 3 bytes
     (positiva mais curta) até o que a maior negativa precisar — sempre
     começa com o SID+0x40 (positiva) ou `0x7F` (negativa).
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
app_ecu_sim.c/.h      — leitura dos potenciômetros (mux), conversão pra
                        unidade de engenharia, guarda o "snapshot" atual
app_ecu_protocol.c/.h — monta o frame de telemetria (§3.1); dispatch dos
                        servicos UDS (§3.2: sessao/seguranca/download/
                        transfer/exit), calcula CRC16 e a key seed->key
app_map_storage.c/.h  — grava/lê o mapa na flash interna (ver §8.4)
app_debug_uart.c/.h   — imprime tudo em texto legível na UART
main.c                — laço principal: le sensores a cada Xms, manda
                        telemetria, processa PDUs UDS recebidos (via
                        callback do CubeMX), decide validação de segurança
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

### 8.3 Recepção do mapa — dispatch de serviços UDS

```c
typedef enum { UDS_SESSION_DEFAULT = 0x01, UDS_SESSION_PROGRAMMING = 0x02 } uds_session_t;

typedef struct {
    uds_session_t session;
    bool          security_unlocked;
    uint16_t      last_seed;          // seed do ultimo requestSeed, pra conferir a key

    bool     download_active;
    uint8_t  table_id;
    uint16_t total_len;
    uint8_t  buf[124];       // APP_MAP_SERIALIZED_LEN
    uint16_t received_len;
    uint8_t  expected_bsc;
    uint8_t  last_bsc;       // permite aceitar repeticao do MESMO bsc (§3.2 passo 4)
} uds_state_t;

static uds_state_t s_uds = { .session = UDS_SESSION_DEFAULT };

static void send_negative(uint8_t sid, uint8_t nrc)
{
    uint8_t pkt[3] = { 0x7F, sid, nrc };
    // Custom_STM_UpdateChar(CUSTOM_STM_UDS_RESPONSE, pkt); // <- ajuste ao nome gerado
    debug_print_uds_response(false, sid, nrc);
}

// Chame isto de dentro do callback que o CubeMX gerar quando a
// characteristic "UDS Request" (escrita, §4) receber dados. Cada PDU e
// despachado pelo SID (primeiro byte) — exatamente como um servidor UDS
// real multiplexa varios servicos num canal so.
void on_uds_request_received(const uint8_t *data, uint16_t len)
{
    if (len < 1) return;
    uint8_t sid = data[0];

    switch (sid) {
    case 0x10: handle_session_control(data, len);        break;
    case 0x27: handle_security_access(data, len);        break;
    case 0x34: handle_request_download(data, len);       break;
    case 0x36: handle_transfer_data(data, len);          break;
    case 0x37: handle_request_transfer_exit(data, len);  break;
    default:   send_negative(sid, 0x11); break;  // serviceNotSupported
    }
}

// 1) DiagnosticSessionControl — unico servico que roda mesmo fora da
// sessao de programacao (obvio: e ele quem entra nela).
static void handle_session_control(const uint8_t *data, uint16_t len)
{
    if (len < 2) { send_negative(0x10, 0x13); return; }  // incorrectMessageLengthOrInvalidFormat

    uint8_t requested = data[1];
    if (requested == UDS_SESSION_PROGRAMMING) {
        ecu_sim_data_t cur;
        read_all_sensors(&cur);
        if (engine_is_running(&cur)) {
            send_negative(0x10, 0x22);  // conditionsNotCorrect — PRINCIPAL PONTO DE RECUSA (ver §3)
            return;
        }
    }

    s_uds.session           = (uds_session_t)requested;
    s_uds.security_unlocked = false;  // trocar de sessao sempre reseta seguranca
    s_uds.download_active   = false;

    uint8_t pkt[3] = { 0x50, requested, 0x00 };  // sessionParameterRecord simplificado
    // Custom_STM_UpdateChar(CUSTOM_STM_UDS_RESPONSE, pkt); // <- ajuste ao nome gerado
    debug_print_uds_response(true, 0x10, 0);
}

// 2) SecurityAccess — seed/key nivel 1. So funciona dentro da sessao de programacao.
static void handle_security_access(const uint8_t *data, uint16_t len)
{
    if (s_uds.session != UDS_SESSION_PROGRAMMING) { send_negative(0x27, 0x7E); return; }
    if (len < 2) { send_negative(0x27, 0x13); return; }

    uint8_t sub = data[1];
    if (sub == 0x01) {  // requestSeed
        s_uds.last_seed = (uint16_t)(HAL_GetTick() & 0xFFFF);
        if (s_uds.last_seed == 0) s_uds.last_seed = 1;  // evita seed=0 (key trivial)

        uint8_t pkt[4] = { 0x67, 0x01, (uint8_t)(s_uds.last_seed & 0xFF), (uint8_t)(s_uds.last_seed >> 8) };
        // Custom_STM_UpdateChar(CUSTOM_STM_UDS_RESPONSE, pkt); // <- ajuste ao nome gerado
        debug_print_uds_response(true, 0x27, 0);

    } else if (sub == 0x02) {  // sendKey
        if (len < 4) { send_negative(0x27, 0x13); return; }
        uint16_t key_recv = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        uint16_t key_exp  = compute_key(s_uds.last_seed);  // ver algoritmo em §3.2
        if (key_recv != key_exp) { send_negative(0x27, 0x35); return; }  // invalidKey

        s_uds.security_unlocked = true;
        uint8_t pkt[2] = { 0x67, 0x02 };
        debug_print_uds_response(true, 0x27, 0);

    } else {
        send_negative(0x27, 0x12);  // subFunctionNotSupported
    }
}

// 3) RequestDownload — declara qual tabela e o tamanho. Exige sessao de
// programacao E seguranca liberada.
static void handle_request_download(const uint8_t *data, uint16_t len)
{
    if (s_uds.session != UDS_SESSION_PROGRAMMING) { send_negative(0x34, 0x7E); return; }
    if (!s_uds.security_unlocked)                  { send_negative(0x34, 0x33); return; }
    if (len < 7)                                   { send_negative(0x34, 0x13); return; }

    uint16_t addr = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    uint16_t size = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
    if (addr > 0x0002 || size != 124) { send_negative(0x34, 0x31); return; }  // requestOutOfRange

    s_uds.download_active = true;
    s_uds.table_id        = (uint8_t)addr;
    s_uds.total_len       = size;
    s_uds.received_len    = 0;
    s_uds.expected_bsc    = 0x01;
    s_uds.last_bsc        = 0x00;

    uint8_t pkt[3] = { 0x74, 0x20, 16 };  // lengthFormatId=1 byte segue, maxBlockLen=16
    debug_print_uds_response(true, 0x34, 0);
}

// 4) TransferData — repetido ate cobrir os 124 bytes.
static void handle_transfer_data(const uint8_t *data, uint16_t len)
{
    if (!s_uds.download_active) { send_negative(0x36, 0x24); return; }  // requestSequenceError
    if (len < 2)                 { send_negative(0x36, 0x13); return; }

    uint8_t bsc = data[1];
    if (bsc == s_uds.last_bsc) {
        // Repeticao valida do bsc anterior (o painel nao recebeu a
        // resposta a tempo) — so reenvia o ACK, nao reprocessa o dado.
        uint8_t pkt[2] = { 0x76, bsc };
        debug_print_uds_response(true, 0x36, 0);
        return;
    }
    if (bsc != s_uds.expected_bsc) { send_negative(0x36, 0x73); return; }  // wrongBlockSequenceCounter

    uint16_t chunk_len = len - 2;
    if (s_uds.received_len + chunk_len > sizeof(s_uds.buf)) { send_negative(0x36, 0x31); return; }

    memcpy(&s_uds.buf[s_uds.received_len], &data[2], chunk_len);
    s_uds.received_len += chunk_len;
    s_uds.last_bsc      = bsc;
    s_uds.expected_bsc++;  // uint8_t: da a volta sozinho 0xFF -> 0x00

    uint8_t pkt[2] = { 0x76, bsc };
    debug_print_uds_response(true, 0x36, 0);
}

// 5) RequestTransferExit — fecha, valida CRC, recheca motor parado (pode
// ter ligado durante a transferencia) e so entao grava na flash.
static void handle_request_transfer_exit(const uint8_t *data, uint16_t len)
{
    if (!s_uds.download_active || s_uds.received_len != s_uds.total_len) {
        send_negative(0x37, 0x24);  // requestSequenceError — download incompleto
        return;
    }
    if (len < 3) { send_negative(0x37, 0x13); return; }

    uint16_t crc_recv = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    uint16_t crc_calc = crc16_ccitt_false(s_uds.buf, s_uds.received_len);
    s_uds.download_active = false;  // a transferencia acaba aqui de qualquer jeito

    if (crc_recv != crc_calc) { send_negative(0x37, 0x72); return; }  // generalProgrammingFailure

    ecu_sim_data_t cur;
    read_all_sensors(&cur);
    if (engine_is_running(&cur)) { send_negative(0x37, 0x22); return; }  // conditionsNotCorrect (2a checagem)

    if (!validate_and_clamp_table(s_uds.buf, s_uds.table_id)) {
        send_negative(0x37, 0x31);  // requestOutOfRange
        return;
    }

    if (!map_storage_save(s_uds.table_id, s_uds.buf, s_uds.received_len)) {
        send_negative(0x37, 0x72);  // generalProgrammingFailure (falha de flash)
        return;
    }

    uint8_t pkt[2] = { 0x77, 0x00 };
    debug_print_uds_response(true, 0x37, 0);
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

// Nomes so dos NRCs que esta ECU efetivamente usa (ver tabela do §3.2) —
// nao e a lista inteira do padrao ISO 14229-1.
static const char *nrc_name(uint8_t nrc)
{
    switch (nrc) {
    case 0x13: return "incorrectMessageLengthOrInvalidFormat";
    case 0x22: return "conditionsNotCorrect";
    case 0x24: return "requestSequenceError";
    case 0x31: return "requestOutOfRange";
    case 0x33: return "securityAccessDenied";
    case 0x35: return "invalidKey";
    case 0x72: return "generalProgrammingFailure";
    case 0x73: return "wrongBlockSequenceCounter";
    case 0x7E: return "subFunctionNotSupportedInActiveSession";
    default:   return "?";
    }
}

static void debug_print_uds_response(bool positive, uint8_t sid, uint8_t nrc)
{
    char line[96];
    int n = positive
        ? snprintf(line, sizeof(line), "[UDS] SID=0x%02X -> positiva (0x%02X)\r\n",
                   sid, (uint8_t)(sid + 0x40))
        : snprintf(line, sizeof(line), "[UDS] SID=0x%02X -> NEGATIVA NRC=0x%02X (%s)\r\n",
                   sid, nrc, nrc_name(nrc));
    HAL_UART_Transmit(&huart1, (uint8_t *)line, n, 100);
}
```
`debug_print_uds_response` já é chamada de dentro de cada `handle_*` do
§8.3 (via `send_negative` na via negativa, direto nos outros) — não
precisa espalhar chamada manual, só usar os handlers como estão.

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
   (§4) e que a Telemetria notifica 18 bytes periodicamente. Depois teste a
   sequência UDS manualmente, escrevendo cada PDU na characteristic "UDS
   Request" e conferindo a resposta na "UDS Response":
   a. `10 02` (session control) → espere `50 02 00` (positiva).
   b. `27 01` (request seed) → anote o seed de 2 bytes que voltar.
   c. Calcule a key com o algoritmo do §3.2 e escreva `27 02 <key_lo> <key_hi>`
      → espere `67 02` (positiva).
   d. `34 00 22 00 00 7C 00` (RequestDownload da tabela 0=injeção, size=124=0x7C)
      → espere `74 20 10` (aceita, maxBlockLen=16).
   e. Só pra confirmar que a máquina de estados aceita dado, escreva UM
      `TransferData` de teste (`36 01` + 16 bytes quaisquer) → espere
      `76 01`. Não precisa completar os 124 bytes só pra este teste manual.
4. **Teste a trava de segurança**: com o potenciômetro de RPM girado
   (>0), repita o passo 3a (`10 02`) — confirme que a ECU responde negativo
   `7F 10 22` (conditionsNotCorrect) e nem chega a entrar em sessão de
   programação. Com o RPM voltando a zero, o mesmo comando deve aceitar.
5. **Teste a segurança errada de propósito**: mande uma key errada no
   passo 3c — confirme `7F 27 35` (invalidKey) e que um `RequestDownload`
   posterior é recusado com `7F 34 33` (securityAccessDenied) por não ter
   passado pela segurança de verdade.
6. **Só depois disso**, conecte de verdade com o painel — o lado do
   painel ainda precisa do GATT client implementado (não existe ainda,
   ver §10) pra rodar essa sequência automaticamente; até lá, os passos
   1-5 acima já validam a ECU sozinha.

## 10. O que falta do lado do painel (fora do escopo deste guia)

Este guia cobre só o firmware da ECU. Pro painel realmente CONSUMIR essa
telemetria e mandar mapas de verdade, falta implementar (no repo
`lvgl_porting`, componente `app_ble`):
- `ble_gattc_disc_svc_by_uuid` → descobrir o serviço pelo UUID de §4.
- `ble_gattc_disc_all_chrs` → descobrir as characteristics.
- `ble_gattc_subscribe` na characteristic de Telemetria → encaminhar bytes
  recebidos pra `app_ecu_feed_ble_notify()` (já existe e já funciona).
- `ble_gattc_subscribe` na characteristic "UDS Response" → encaminhar cada
  PDU pra `app_map_parse_response()` (já existe e já testado do lado do
  painel) e dar sequência à próxima etapa (sessão → segurança → download →
  transfer → exit) conforme a resposta.
- `ble_gattc_write` pra characteristic "UDS Request" → é o que
  `app_map_send_to_ecu()` vai chamar de verdade pra cada PDU da sequência
  (montados por `app_map_build_session_control`/`_security_seed_request`/
  `_security_send_key`/`_request_download`/`_transfer_data_pdus`/
  `_transfer_exit`, todos já implementados e testados). Hoje
  `app_map_send_to_ecu()` é um stub, retorna `ESP_ERR_NOT_SUPPORTED`.

Isso é trabalho separado, do lado ESP32 — depois que a ECU (este guia)
estiver funcionando e você confirmar os UUIDs/comportamento reais com um
app de BLE genérico, essa é a próxima peça a pedir.

## 11. Limitações conhecidas deste simulador

- Não lê motor real nenhum — é só potenciômetro simulando valor.
- **O `SecurityAccess` (§3.2) não é criptografia real** — é uma
  transformação simples (rotação de bits + XOR) só pra impedir escrita
  acidental de um app BLE genérico, não pra resistir a alguém que capturou
  o tráfego (o algoritmo está documentado em texto claro neste próprio
  guia). Pra um produto real, troque por um desafio-resposta mais forte
  (ex.: HMAC com uma chave que não viaje em texto claro em documentação
  nenhuma) e some isso ao pareamento BLE (bonding) — hoje não tem nenhum
  dos dois além do gate seed/key.
- Sem autenticação/pareamento BLE (bonding/pairing) — qualquer dispositivo
  próximo pode se conectar (a única barreira pra ESCREVER um mapa é o
  SecurityAccess acima; LER a telemetria continua livre pra qualquer
  central). Aceitável pra bancada de teste; **não é adequado pra um
  produto final** sem revisar isso (bonding/pairing do NimBLE/STM32WB,
  fora do escopo aqui).
- `validate_and_clamp_table` (§8.3) clampa em vez de rejeitar por padrão —
  troque pra rejeitar (`return false`) se preferir uma política mais
  estrita (a diferença é: clampar aceita o mapa mas força os valores pra
  dentro da faixa seguro; rejeitar devolve `requestOutOfRange` e não muda
  nada). Isso é uma escolha de produto, não uma resposta técnica única.
- O padrão UDS real reconhece **repetição do mesmo `blockSequenceCounter`**
  como reenvio válido (§3.2, passo 4) — mas não tem um mecanismo de
  retransmissão automática embutido além disso; se o painel perder uma
  resposta e não reenviar por conta própria, a transferência trava até dar
  timeout do lado dele. Pra bancada de teste isso é aceitável.
