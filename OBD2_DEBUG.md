# OBD2 Debug via UART

Debug logs foram adicionados ao firmware para rastrear requisições/respostas OBD2. Use os scripts abaixo para monitorar apenas essas mensagens.

## Opção 1: Monitor Simples (Recomendado)

```bash
./obd2_monitor_simple.sh /dev/ttyACM0
```

Mostra apenas logs `[OBD2-*]` com emoji colorido.

**Símbolos:**
- 📤 `[OBD2-TX]` — Requisição saindo (qual PID, de quantos)
- 📥 `[OBD2-RX]` — Resposta recebida (valores decodificados)
- ⏱️ `[OBD2-STALE]` — Sem resposta por 2s (timeout)
- 📊 `[OBD2-GET]` — Leitura de dados (dashboard/decoder)
- ⚙️ `[OBD2]` — Ativação/desativação do modo

## Opção 2: Monitor Avançado

```bash
./obd2_monitor.sh /dev/ttyACM0
```

Versão com cores mais ricas e legenda na inicialização.

## Opção 3: Manual (grep)

Se preferir controlar o monitor sozinho:

```bash
source /home/zotti/.espressif/v5.5.4/esp-idf/export.sh
idf.py -p /dev/ttyACM0 monitor 2>&1 | grep "\[OBD2-"
```

## Teste Prático

**Hardware:**
1. Vectra com ignição ligada
2. CAN conectado ao ESP32 (pins BSP_CAN_TX e BSP_CAN_RX)

**Software:**
1. Flash do firmware (via VSCode extension ou `idf.py flash`)
2. Rodar monitor: `./obd2_monitor_simple.sh`
3. No painel, entrar em **CAN Decoder** ou **Dashboard**
4. Clicar botão **"CAN"** (ficará vermelho)

**Esperado:**
- Cada 150ms: `📤 [OBD2-TX] Requisição N/7: PID=0xXX`
- Imediatamente após (se conectado): `📥 [OBD2-RX] PID=0xXX ... rpm=XXXX spd=XX ...`
- A cada segundo: `📊 [OBD2-GET] rpm=XXXX spd=XX ...` (dashboard lendo snapshot)

**Se não houver resposta:**
- 2s depois de TX: `⏱️ [OBD2-STALE] Sem resposta por 2000ms...`
- Dashboard mostra "CAN/OBD2 lendo (transmitindo)" em vermelho (inválido)

## Diagnóstico

| Sintoma | Causa Provável | Ação |
|---------|---|---|
| Apenas TX, nunca RX | Vectra não responde / CAN desconectado | Verificar conexão física, testar com sniffer passivo |
| TX timeout logo | Driver TWAI não iniciou | Ver logs `[OBD2]` e `[APP_CAN]` antes de clicar botão |
| RX com valores estranhos | Fórmula SAE J1979 errada | Comparar com ferramenta OBD2 comercial |
| RX intermitente | Vectra resguardada ou ECU dormindo | Acelerar motor, desligar/religar |

## Logs Detalhados (Exemplo)

```
📤 [OBD2-TX] Requisição 1/7: PID=0x0C
📥 [OBD2-RX] PID=0x0C A=18.00 B=200.00 | rpm=2784 spd=0 ect=-40 iat=-40 map=0 tps=0 batt=0.00

📤 [OBD2-TX] Requisição 2/7: PID=0x0D
📥 [OBD2-RX] PID=0x0D A=65.00 B=0.00 | rpm=2784 spd=65 ect=-40 iat=-40 map=0 tps=0 batt=0.00

📤 [OBD2-TX] Requisição 3/7: PID=0x05
📥 [OBD2-RX] PID=0x05 A=96.00 B=0.00 | rpm=2784 spd=65 ect=56 iat=-40 map=0 tps=0 batt=0.00

...

📊 [OBD2-GET] rpm=2784 spd=65 ect=56 iat=-40 map=98 tps=25 batt=14.23
```

## Desabilitar logs (quando operacional)

Se quiser remover esses logs no futuro (economizar UART bandwidth):
1. Remover `ESP_LOGI/ESP_LOGW` das funções em `app_bcu.c`
2. Manter a lógica intacta
3. Recompilar
