# Roadmap — Painel + ECU programável + Módulo de pedal

> Este arquivo documenta a arquitetura do sistema completo (3 placas) e as
> decisões já tomadas, para não perder o fio da meada entre sessões. Para
> hardware/toolchain **desta placa** (Waveshare ESP32-S3-Touch-LCD-4.3B), ver
> [README.md](README.md) — não duplicado aqui.

## 1. Visão geral do sistema (3 hardwares diferentes)

```
┌───────────────────────────┐   BLE (notify, somente leitura)   ┌──────────────────────────┐
│ PAINEL (este repo)        │ ◄───────────────────────────────  │ ECU PROGRAMÁVEL (nova)   │
│ Waveshare ESP32-S3-4.3"   │                                    │ até 4 cilindros          │
│ - Tela / LVGL             │        CAN (com outros módulos)   │ - Injeção/ignição        │
│ - CAN sniffer (Vectra)    │ ◄────────────────────────────────►│ - Sensores motor         │
│ - SD, BLE central         │                                    └──────────────────────────┘
└─────────────┬─────────────┘
              │ UART/RS485 (heartbeat + modo / telemetria)
              ▼
┌───────────────────────────┐
│ MÓDULO DE PEDAL (novo)    │
│ MCU dedicado + relé       │
│ de bypass em hardware     │
│ (failsafe autônomo)       │
└───────────────────────────┘
```

**Regra de ouro, não é opcional:** o painel é só um **mostrador/telemetria**.
Ele nunca manda comando de controle para a ECU (só assina notificações BLE) e
nunca é responsável pelo failsafe do pedal (isso é hardware, no módulo de
pedal). Se algum dia isso mudar, precisa ser decisão explícita e revisada,
não um acidente de arquitetura.

## 2. O que já existe neste repo (implementado)

| Componente | Função | Status |
|---|---|---|
| `components/app_can/` | Sniffer CAN passivo (Vectra), log CSV no SD, task própria core 0 | Funcionando |
| `components/app_ble/` | Scan/connect BLE genérico (NimBLE) | Funcionando (scan/connect only, sem GATT client ainda) |
| `components/ui/` | Telas LVGL (menu, dashboard, CAN sniffer, SD browser, config c/ scan BLE) | Funcionando |
| `components/app_ecu/` **(novo)** | Modelo de dados + parser do protocolo de telemetria BLE v1 (ver §4) | Parser completo, testado (Unity); **falta o GATT client** (ver §6) |
| `components/app_pedal_link/` **(novo)** | Comunicação UART com o módulo de pedal via RS485 (GPIO43/44) | Parser completo e testado; ⚠️ **pino em conflito conhecido, ver §5** |
| `components/app_sim/` **(novo)** | Modo demo/simulação — gera RPM/velocidade/aceleração/sondas plausíveis, ciclo com rajadas perto do corte | Funcionando, testado (Unity) |
| `components/app_dash_profile/` **(novo)** | Perfis salvos do dashboard em NVS (estilo, redline, cor) | Funcionando |
| `ui_screen_ecu.c` | Tela "MONITOR ECU BLE" | Lê dados reais de `app_ecu` a cada 300ms (antes só mostrava "---") |
| `ui_screen_dashboard.c` | Dashboard | Agora tem: gauge digital/analógico alternável, vários perfis salvos (NVS), efeito de "perto do corte", modo Demo com dados simulados — ver §10 |
| `test_app/` **(novo)** | App de teste Unity separado (`idf.py -C test_app build flash monitor`) | Cobre app_ecu, app_pedal_link (parser), app_sim |

### Achado durante a revisão: código morto
`ui_screen_calculos.c` / `ui_tabs.c` (a aba "Cálculos" e o sistema de tabs
legado) **não são chamados por nenhum caminho ativo da tela** — `ui_init()`
vai direto para `ui_splash_show()` → menu → telas novas. Deixei como está;
avisa se quiser reaproveitar ou apagar.

## 3. Por que a SD travava — CORRIGIDO

`ui_screen_sd_browser_show()` fazia montagem+listagem do cartão **de forma
síncrona dentro da task do LVGL** (via `lv_async_call`, que só adia — não
tira do mesmo thread). Corrigido: `app_can.c` agora tem uma **task dedicada
de SD** (`sd_worker_task`) + fila de pedidos (`app_can_sd_async_*` no
header). `ui_screen_sd_browser.c` foi reescrito pra só enfileirar pedidos
(mount/list/delete/format) e aplicar o resultado na tela via `lv_async_call`
quando o worker termina — nunca bloqueia a task do LVGL.

**Incidente em teste real (2026-07-19):** ao abrir a tela de SD pelo hardware
de verdade, `taskLVGL` travou dentro de `build_list()` criando os botões da
lista (watchdog disparando, heap parado). Duas hipóteses investigadas:
1. `s_entry_count` maior que o array `s_entries[48]` — corrigido
   (`app_can_sd_async_get_dir_result` devolvia contagem não limitada; e
   `build_list()`/`apply_dir_result_async` agora trunca por segurança).
2. Estouro de pilha na `sd_worker_task` corrompendo o heap (o array local
   `tmp[48]` de ~2.3KB somado à profundidade FATFS/VFS/SDSPI) — removido:
   `sd_worker_task` agora escreve direto no buffer compartilhado, sem cópia
   local. Também adicionei log `[DIAG-SD-TASK] stack_free=...` (mesmo
   padrão do `[DIAG-LVGL-TASK]` já existente) pra ter evidência direta se
   isso apertar de novo.

Ainda não confirmado em hardware se isso resolveu — próximo teste real vai
dizer. Se persistir, o log `[DIAG-SD-TASK]` novo deve apontar o culpado.

**Ponto residual conhecido, não corrigido (menor impacto):**
`app_can_sniffer_start()` (chamado por `ui_screen_can_sniffer_show()` e pelo
botão START/STOP) ainda chama `app_can_sd_mount()` de forma síncrona. Na
prática só bloqueia de verdade se o cartão **não** tiver sido montado no
boot (ex: cartão inserido depois) — o caso comum (já montado) retorna na
hora. Se isso incomodar, dá pra trocar essas duas chamadas por
`app_can_sd_async_mount()` seguindo o mesmo padrão.

## 4. Protocolo ECU → Painel (BLE notify), v1

Definido em [`components/app_ecu/include/app_ecu.h`](components/app_ecu/include/app_ecu.h).
Só a ECU fala; o painel só escuta (sem characteristic de escrita usada).

```
[0xEC][versão=0x01][tamanho N][payload N bytes][checksum XOR]
```

Payload v1 (14 bytes, little-endian): `rpm(u16) map_kpa(u8) tps_pct(u8)
ect_c(i8) iat_c(i8) batt_mv(u16) lambda_x1000(u16) uptime_ms(u32)`.

**Falta decidir junto com o firmware da ECU:** UUID do serviço/characteristic
BLE. Assim que a ECU tiver isso definido, o lado do painel some com:
`ble_gattc_disc_svc_by_uuid` → `ble_gattc_disc_all_chrs` → `ble_gattc_subscribe`
(NimBLE), encaminhando os bytes recebidos para `app_ecu_feed_ble_notify()`
(que já existe e já funciona).

## 5. Protocolo Painel ↔ Módulo de pedal (UART/RS485)

Definido em [`components/app_pedal_link/include/app_pedal_link.h`](components/app_pedal_link/include/app_pedal_link.h),
implementado em `app_pedal_link.c`.

⚠️ **Conflito de pino descoberto em teste real, NÃO USAR AINDA sem
verificar:** a doc da Waveshare lista GPIO 43/44 como "RS485 onboard", mas o
log de boot real deste projeto mostra `GPIO 44 and 43 are used as console
UART I/O pins` — são os mesmos pinos do console/USB de depuração
(`/dev/ttyACM0`, usado o tempo todo no `idf.py monitor`). Duas explicações
possíveis: (a) a placa multiplexa um único UART entre "modo debug via USB" e
"modo RS485" (não os dois ao mesmo tempo — você usaria um OU outro conforme
a fase do projeto); (b) a doc do fabricante é de outra revisão de placa.
**Confirme no esquemático/serigrafia física da sua placa antes de ligar
qualquer fio no módulo de pedal.** O código (`app_pedal_link.c`,
`bsp_waveshare_43.h`) já está com aviso nos comentários; os pinos ficaram
como estavam (43/44) só porque não há outro par TX/RX livre confirmado no
momento (ver `GPIO 6` como único candidato avulso, insuficiente sozinho).

```
Comando   (painel → pedal): [0xAA, modo, checksum]        reforçado a cada 1s
Heartbeat (painel → pedal): [0x55]                          a cada 200ms
Telemetria(pedal → painel): [0xBB, pedal_pct, saida_pct, fault_flags, checksum]
```

Se o heartbeat parar, **o módulo de pedal decide sozinho** entrar em
passthrough puro — essa lógica não mora no painel.

⚠️ **Confirmar no esquemático do módulo de pedal** (ainda a projetar) se o
transceiver RS485 da placa principal precisa de controle manual de direção
(DE/RE) ou se é de troca automática — isso muda o driver.

## 6. Referência OBD2 padrão (SAE J1979 / ISO 15765-4) — para o Vectra

Se em algum momento quiser ler o ECU **de fábrica** do Vectra via OBD2 (modo
Mode 01), o CAN já está no barramento certo (500kbps, 11-bit — já configurado
em `app_can.c`). Precisa trocar `TWAI_MODE_LISTEN_ONLY` → `TWAI_MODE_NORMAL`
(listen-only não transmite). Requisição em `0x7DF`, resposta em `0x7E8+`:

| PID | Nome | Fórmula |
|---|---|---|
| 0x0C | RPM | `((A*256)+B)/4` |
| 0x0D | Velocidade | `A` km/h |
| 0x05 | Temp. arrefecimento | `A-40` °C |
| 0x0F | Temp. ar admissão | `A-40` °C |
| 0x0B | MAP | `A` kPa |
| 0x11 | TPS | `A*100/255` % |
| 0x42 | Tensão bateria | `((A*256)+B)/1000` V |

Isso é **independente** da ECU programável — seria uma segunda fonte de
dados (o carro de fábrica), não conflita com o app_ecu.

## 7. Hardware da ECU programável — pontos de partida (pesquisa)

**Bases open-source pra estudar antes de desenhar do zero:**
- Speeduino — firmware [github.com/speeduino/speeduino](https://github.com/speeduino/speeduino), **hardware KiCad real**: [github.com/speeduino/Hardware](https://github.com/speeduino/Hardware)
- rusEFI — [github.com/rusefi/rusefi](https://github.com/rusefi/rusefi), placa pequena 1-4 cilindros com esquemático real: [github.com/rusefi/hw_microRusEfi](https://github.com/rusefi/hw_microRusEfi)
- MegaSquirt: **não é open** (esquemático fechado) — só referência de conceito, não de arquivo pra copiar.

**Chips automotivos (AEC-Q100) por subsistema** — checar datasheet atual antes de comprar, isso muda:
| Subsistema | Chip sugerido | Fabricante |
|---|---|---|
| Driver injetor (proteção integrada) | BTS3050EJ / BTS3125EJ (HITFET+) | Infineon |
| IC integrado de gerenciamento (alternativa "tudo em um" p/ 4 cil) | TLE8888QK | Infineon |
| Driver de ignição | FAD1100-F085 / ISL9V5045S3ST | onsemi |
| Condicionamento sinal roda fônica/cam (VR+Hall) | MAX9924-MAX9927, ou L9788 | ADI / ST |
| Transceiver CAN automotivo | TJA1051 / TJA1042 | NXP |
| Proteção alimentação 12V (load-dump/polaridade reversa) | AP74502Q / MAX16126 | Diodes Inc. / ADI |
| **H-bridge de uso geral (reaproveitável em outros projetos)** | **DRV8873-Q1** | TI |

Guarde o `DRV8873-Q1` — automotivo, proteção integrada, e serve pra outros
projetos seus além da ECU, como você pediu.

## 8. Acesso remoto ao Claude Code (pelo celular)

Existe **Remote Control**: rode `/remote-control` (ou `claude remote-control`)
numa sessão no computador, e você continua/manda novas instruções pelo app
oficial Claude (iOS/Android, aba "Code", escaneando o QR code) ou por
`claude.ai/code` no navegador. O computador precisa continuar rodando o
Claude Code (a sessão reconecta se ele hibernar). Exige login
`claude auth login` com assinatura Pro/Max/Team/Enterprise — chave de API
não funciona pra isso. Alternativa sem depender do PC ligado: sessão cloud
direto em `claude.ai/code`, mas aí sem acesso ao seu filesystem local.

## 10. Dashboard personalizado + modo Demo

`ui_screen_dashboard.c` ganhou:
- **Digital vs Analógico**: alternável (botão dentro do modal "Perfis"). Digital
  = número grande (como já era). Analógico = anel mais fino + 6 marcadores de
  graduação ao redor + o "knob" do arco funcionando como ponteiro (o LVGL já
  posiciona sozinho na ponta do valor — sem precisar de rotação manual de
  agulha, que seria mais frágil de acertar sem poder testar visualmente).
- **Vários perfis salvos** (`components/app_dash_profile/`, NVS, até
  `APP_DASH_PROFILE_MAX`=8): nome, estilo, RPM de corte, tema de cor (índice —
  a paleta em si ainda não está implementada, só o campo). Botão "Perfis" no
  cabeçalho abre lista (Usar/Apagar) + editor do perfil ativo (estilo,
  corte -/+, "Salvar como novo").
- **Efeito de corte**: quando RPM ≥ 90% do redline do perfil ativo, o
  arco/número pisca entre branco e vermelho (`lv_anim`, com histerese em 85%
  pra não ficar oscilando na borda).
- **Animação suave do ponteiro**: o arco anima entre o valor antigo e o novo
  (180ms) em vez de saltar — `animate_arc_to()`.
- Continua sem popular: `AFR`/`ECT`/`IAT` agora aparecem (antes 3 cards
  estavam comentados) — todos vêm de `app_ecu`/`app_sim`, nenhum cálculo novo
  de unidade foi necessário além do lambda→AFR (`lambda*14.7`, já existia).

**Modo Demo** (`components/app_sim/`, completamente separado de
`app_ecu`/`app_can` de propósito): botão "Demo" no cabeçalho do dashboard liga
uma simulação de ciclo de condução (fases idle → aceleração → cruzeiro →
perto do corte → desaceleração, com filtro passa-baixa pra suavizar) gerando
RPM, velocidade, aceleração longitudinal estimada, MAP, TPS, ECT (aquece aos
poucos), IAT, bateria e lambda plausíveis — sem precisar de ECU nem CAN
conectados. Enquanto ligado, um badge amarelo "DEMO" aparece na tela e o
status muda de cor. Nunca escreve em nada relacionado a dado real (SD, log,
app_ecu) — é só uma fonte de leitura alternativa que a UI escolhe usar.

## 11. Testes automatizados

Projeto de teste separado em `test_app/` (não faz parte do firmware
principal, não builda LVGL/tela) — padrão Unity do ESP-IDF:

```
idf.py -C test_app build flash monitor
```

Cobre (todos `TEST_CASE`, auto-registrados, rodam com `unity_run_all_tests()`
em `test_app/main/test_main.c`):
- **app_ecu**: frame v1 válido decodifica certo, checksum inválido rejeitado,
  versão desconhecida rejeitada, frame truncado rejeitado, desconectar invalida
  o último dado.
- **app_pedal_link**: telemetria válida atualiza status, checksum inválido
  não conta como frame ok, lixo antes do start byte é ignorado.
- **app_sim**: desligado não atualiza o snapshot exposto, ligado os valores
  ficam dentro de faixas fisicamente plausíveis (RPM≤redline, bateria
  12-15.5V, lambda 0.7-1.3 etc.), redline configurável e valor 0 é ignorado.

**Decisão de design importante**: os testes de `app_pedal_link` chamam só
`app_pedal_link_feed_bytes()` (parser puro), **nunca** `app_pedal_link_init()`
— essa função mexe em UART/GPIO de verdade nos pinos com conflito conhecido
(§5). Por isso também tirei a dependência de `bsp_waveshare_43` do
`app_pedal_link` (os 2 números de pino viraram `#define` locais duplicados,
comentados apontando pra fonte canônica) — sem isso, o `test_app` arrastaria
toda a pilha de LCD/touch/LVGL só pra compilar um teste de parser.

Não incluí teste automatizado pra `app_dash_profile` (é CRUD fino sobre NVS,
menor risco) nem para as telas LVGL (não dá pra testar UI de verdade sem o
display — ver skill `verify`/`run` pra isso).

## 12. Não coberto nesta rodada (ainda pendente, não esquecido)

- Tela de controle do módulo de pedal (escolher modo Economia/Normal/Sport) —
  `app_pedal_link` já expõe tudo, só falta a tela.
- Telas com stub/gap conhecidos e não tocados: `app_inputs.c`/`app_outputs.c`
  (placeholders), TODOs em `ui_screen_scanner.c`/`ui_screen_config.c`.
- Paleta de cor por perfil (`color_theme` já existe no struct, mas nada lê
  esse campo pra aplicar cor ainda).
- Nomeação customizada de perfil (hoje é automática: "Perfil N") — precisaria
  de um teclado LVGL (`lv_keyboard`), deixado de fora por risco/tempo.

## 9. Próximos passos (em ordem sugerida)

1. **Confirmar a pendência de pino do §5** antes de ligar qualquer fio físico
   no módulo de pedal — bloqueia fisicamente o resto do módulo de pedal.
2. Decidir e implementar o GATT client BLE em `app_ble.c`/`app_ecu.c` assim
   que o firmware da ECU definir o UUID do serviço/characteristic (§4).
3. Retestar em hardware se o travamento do SD (§3) foi realmente resolvido.
4. Projetar o hardware do módulo de pedal (schematic + PCB) com os chips
   automotivos de referência (§7) — H-bridge DRV8873-Q1 primeiro, por ser
   reaproveitável.
5. Adicionar tela de controle do módulo de pedal (§12).
6. Decidir se `ui_screen_calculos.c`/`ui_tabs.c` (código morto, §2) é
   reaproveitado ou removido.
7. Opcional: OBD2 ativo sobre o CAN do Vectra (§6), como segunda fonte de
   telemetria independente da ECU programável.
