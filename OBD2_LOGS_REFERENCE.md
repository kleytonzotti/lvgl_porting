# OBD2 Logs — Referência (Desativado por Padrão)

Todos os logs OBD2 estão **comentados** no código. Esta página documenta o que cada um faz, para referência futura.

## Reativar Logs

Para reativar um log específico, descomente a linha correspondente em [components/app_bcu/src/app_bcu.c](components/app_bcu/src/app_bcu.c).

---

## Logs Disponíveis

### 1. `[OBD2-TX]` — Requisição OBD2 saindo

**Localização:** `obd2_poll_step()`, line ~338

**O que faz:**
```
[OBD2-TX] Requisição N/7: PID=0xXX
```
- **N** = posição no round-robin (1-7)
- **PID** = qual parâmetro está sendo requisitado
- Acontece a cada 150ms (1 PID por vez)

**Sem este log:** Você não sabe se as requisições estão saindo do ESP32.

**Quando ativar:** Se suspeitar que o TWAI não está transmitindo.

---

### 2. `[OBD2-RX]` — Resposta OBD2 recebida + valores decodificados

**Localização:** `obd2_decode_frame_locked()`, line ~310

**O que faz:**
```
[OBD2-RX] PID=0x0C A=18.00 B=200.00 | rpm=2784 spd=0 ect=-40 iat=-40 map=0 tps=0 batt=0.00
```
- **PID** = qual resposta chegou
- **A, B** = bytes brutos da resposta
- **rpm, spd, ect, iat, map, tps, batt** = valores já decodificados pela fórmula SAE J1979

**Sem este log:** Você vê os dados no dashboard, mas não sabe se vieram do barramento ou são cached.

**Quando ativar:** Para validar se as respostas estão chegando e se as fórmulas de decodificação estão corretas.

---

### 3. `[OBD2-STALE]` — Timeout de 2 segundos sem resposta

**Localização:** `obd2_poll_step()`, line ~328

**O que faz:**
```
[OBD2-STALE] Sem resposta por 2000ms, marcando dados como inválidos
```
- A cada ciclo, verifica se passaram 2s desde a última resposta
- Se sim, marca `valid=false` no snapshot
- Dashboard e decoder saberão que os dados não são mais atuais

**Sem este log:** Você não sabe quando os dados foram invalidados por silêncio.

**Quando ativar:** Para debugar por que o dashboard mostra "CAN/OBD2 lendo (transmitindo)" em vermelho.

---

### 4. `[OBD2-GET]` — Dashboard/Decoder lendo snapshot

**Localização:** `app_bcu_obd2_get_data()`, line ~890 e ~898

**Tem dois sub-tipos:**

#### 4a. Dados ficaram stale durante leitura
```
[OBD2-GET] Dados ficaram stale durante leitura (silence=2000ms)
```
- Ocorre quando o `get_data()` verifica idade dos dados
- Se ficou stale entre a cópia do snapshot e a invalidação final, avisa

#### 4b. Snapshot lido com sucesso (a cada 1s)
```
[OBD2-GET] rpm=2784 spd=65 ect=56 iat=-40 map=98 tps=25 batt=14.23
```
- Mostra o que o dashboard/decoder está vendo, a cada 1 segundo

**Sem este log:** Você não sabe qual estado o consumidor (UI) está recebendo.

**Quando ativar:** Para correlacionar o que o barramento forneceu com o que a UI mostra.

---

### 5. `[OBD2]` — Ativação/Desativação da fonte OBD2

**Localização:** `app_bcu_obd2_set_active()`, line ~867

**O que faz:**
```
[OBD2] Ativação=1 | Modo TWAI=NORMAL (TRANSMITE PIDs) | Driver=1 | Sniffer=OFF durante OBD2
```
- Quando o botão "CAN" é clicado no dashboard/decoder
- Mostra se foi ligado (1) ou desligado (0)
- Modo do driver (NORMAL = transmitindo, LISTEN_ONLY = só escutando)
- Se o driver TWAI foi iniciado com sucesso

**Sem este log:** Você não sabe se o clique no botão realmente ativou o modo.

**Quando ativar:** Para confirmar que a mudança de modo está funcionando.

---

### 6. `[OBD2-TX] Ignorado/Falha` — Erros de transmissão

**Localização:** `app_bcu_obd2_request_pid()`, line ~906 e ~920

#### 6a. Transmissão ignorada (estado inválido)
```
[OBD2-TX] Ignorado: ativo=0 started=0
```
- Tentativa de pedir um PID quando OBD2 não está ativo

#### 6b. Falha ao transmitir
```
[OBD2-TX] Falha ao transmitir PID 0x0C: err=265
```
- Frame CAN não conseguiu entrar na fila TWAI
- Código de erro ESP-IDF

**Sem este log:** Você nunca saberia se o frame foi realmente enviado.

**Quando ativar:** Para diagnosticar problemas de congestão de barramento ou driver TWAI.

---

## Relação entre Logs

Fluxo esperado (com todos ativados):

```
[OBD2] Ativação=1 | Modo TWAI=NORMAL | ...
📤 [OBD2-TX] Requisição 1/7: PID=0x0C
   (aguarda ~100ms)
📥 [OBD2-RX] PID=0x0C A=18.00 B=200.00 | rpm=2784 ...
📤 [OBD2-TX] Requisição 2/7: PID=0x0D
📥 [OBD2-RX] PID=0x0D A=65.00 B=0.00 | rpm=2784 spd=65 ...
...
📊 [OBD2-GET] rpm=2784 spd=65 ...  (cada 1s)
```

**Se pular direto para stale:**
```
[OBD2-TX] Requisição 1/7: PID=0x0C
[OBD2-TX] Requisição 2/7: PID=0x0D
[OBD2-TX] Requisição 3/7: PID=0x05
[OBD2-STALE] Sem resposta por 2000ms...
```
→ Vectra não está respondendo (desconectada, motor desligado, etc.)

---

## Referência Rápida para Descomentação

| Log | Arquivo | Função | Linha | Use se... |
|-----|---------|--------|-------|-----------|
| `[OBD2-TX]` | app_bcu.c | `obd2_poll_step()` | ~338 | Quiser ver todas as requisições |
| `[OBD2-RX]` | app_bcu.c | `obd2_decode_frame_locked()` | ~310 | Quiser validar decodificação |
| `[OBD2-STALE]` | app_bcu.c | `obd2_poll_step()` | ~328 | Quiser ver timeouts |
| `[OBD2-GET]` (stale) | app_bcu.c | `app_bcu_obd2_get_data()` | ~890 | Diagnosticar invalidação |
| `[OBD2-GET]` (values) | app_bcu.c | `app_bcu_obd2_get_data()` | ~898 | Ver dados lidos a cada 1s |
| `[OBD2]` | app_bcu.c | `app_bcu_obd2_set_active()` | ~867 | Confirmar ativação/desativação |
| `[OBD2-TX] erros` | app_bcu.c | `app_bcu_obd2_request_pid()` | ~906, ~920 | Debugar falhas de TX |

---

## Impacto de Performance

- Cada log descomentado = ~5-10µs por ocorrência (tempo de `ESP_LOG*`)
- Com todos 7 logs ativos: ~50µs extra por ciclo de 150ms = **negligenciável**
- **Não há risco de travamento** ao desativar/ativar logs

---

## Monitorar sem Recompilar

Se quiser ver logs **sem recompilar**, use os helpers:

```bash
./obd2_monitor_simple.sh /dev/ttyACM0
```

(reativa logs via monitor apenas durante a sessão)
