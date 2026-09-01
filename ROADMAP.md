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
│ - BCU Monitor (Vectra)    │ ◄────────────────────────────────►│ - Sensores motor         │
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
| `components/app_bcu/` | Sniffer CAN passivo (Vectra), log CSV no SD, task própria core 0 | Funcionando |
| `components/app_ble/` | Scan/connect BLE genérico (NimBLE) | Funcionando (scan/connect only, sem GATT client ainda) |
| `components/ui/` | Telas LVGL (menu, dashboard, BCU Monitor, SD browser, config c/ scan BLE) | Funcionando |
| `components/app_ecu/` **(novo)** | Modelo de dados + parser do protocolo de telemetria BLE v1 (ver §4) | Parser completo, testado (Unity); **falta o GATT client** (ver §6) |
| `components/app_pedal_link/` **(novo)** | Comunicação UART com o módulo de pedal via RS485 (GPIO43/44) | Parser completo e testado; ⚠️ **pino em conflito conhecido, ver §5** |
| `components/app_sim/` **(novo)** | Modo demo/simulação — gera RPM/velocidade/aceleração/sondas plausíveis, ciclo com rajadas perto do corte | Funcionando, testado (Unity) |
| `components/app_dash_profile/` **(novo)** | Perfis salvos do dashboard em NVS (estilo, redline, cor) | Funcionando |
| `ui_screen_ecu.c` | Tela "MONITOR ECU BLE" | Lê dados reais de `app_ecu` a cada 300ms (antes só mostrava "---") |
| `ui_screen_dashboard.c` | Dashboard | 4 modelos de tela (Classic/Race/Grid/Duplo), config única persistida em NVS (editada em Configurações), efeito de "perto do corte", modo Demo, cor do acento do RPM (Grid) — ver §10 |
| `ui_screen_pedal.c` **(novo)** | Tela "MODULO DE PEDAL" — seleção de modo Economia/Normal/Sport | Usa `app_pedal_link_set_mode`/`get_status`; mostra "DESCONECTADO" corretamente enquanto §5 não for resolvido (init continua desligado de propósito) |
| `ui_screen_can.c` (aba Decoder) | OBD2 ativo sobre o CAN do Vectra (SAE J1979 Mode 01) | Botão liga/desliga `app_bcu_obd2_set_active`; só **exibe** o snapshot — quem pede/decodifica é o `app_bcu` (§6) |
| `ui_screen_bcu_trip.c` **(novo)** | Tela "COMPUTADOR DE BORDO" (menu, antigo "Scanner") — velocidade em destaque + distância/vel. média/vel. máxima/tempo de viagem, botão Zerar, indicador de Cruise Control | Fonte CAN (OBD2, padrão) ou Demo — sem opção ECU BLE porque `app_ecu` não carrega velocidade; distância integrada localmente (velocidade × dt) na própria tela, não persiste em NVS/SD; Cruise Control é só um indicador visual local, **nunca** um comando — ver regra de ouro do §1 |
| `components/app_map/` **(novo)** | Mapas de tunagem (injeção/ignição/sonda-lambda alvo), RPM x carga (kPa) — modelo de dados, cache local em NVS, framing do protocolo BLE de escrita (chunk+CRC16) | Lógica pura testada (Unity); `app_map_send_to_ecu()` é stub honesto — falta o GATT client de escrita, ver §13 |
| `ui_screen_map.c` **(novo)** | Tela "MAPAS" (menu, antigo "Data Logger") — grade heatmap (estilo FuelTech/Injepro) com 3 abas Injeção(ms)/Ignição(°)/Sonda(lambda) por RPM x kPa, toque pra selecionar célula + passos, botão "Salvar Mapa" | Só grava (NVS local + tentativa de envio BLE) quando o usuário clica Salvar — editar não persiste nada sozinho; abas além da primeira só são construídas quando visitadas (pool de 64KB da LVGL, ver aviso em `ui_screen_map.c`); ver §13 pra por que isto é uma exceção deliberada à regra de ouro do §1 |
| `test_app/` **(novo)** | App de teste Unity separado (`idf.py -C test_app build flash monitor`) | Cobre app_ecu, app_pedal_link (parser), app_sim, app_map (framing/CRC) |

### Código morto removido
`ui_screen_calculos.c`, `ui_tabs.c`, `ui_screen_animacao.c`,
`ui_screen_entradas.c`, `ui_screen_saidas.c`, `ui_screen_botoes.c` e a função
legada `ui_splash_create()` não eram chamados por nenhum caminho ativo da
tela (`ui_init()` vai direto para `ui_splash_show()` → menu → telas novas) —
apagados. `ui_screen_touch_create()` foi mantida: é usada de verdade por
`ui_screen_sistema.c`, só também era chamada (redundantemente) pelo
`ui_tabs.c` morto.

## 3. Por que a SD travava — CORRIGIDO

`ui_screen_sd_browser_show()` fazia montagem+listagem do cartão **de forma
síncrona dentro da task do LVGL** (via `lv_async_call`, que só adia — não
tira do mesmo thread). Corrigido: `app_bcu.c` agora tem uma **task dedicada
de SD** (`sd_worker_task`) + fila de pedidos (`app_bcu_sd_async_*` no
header). `ui_screen_sd_browser.c` foi reescrito pra só enfileirar pedidos
(mount/list/delete/format) e aplicar o resultado na tela via `lv_async_call`
quando o worker termina — nunca bloqueia a task do LVGL.

**Incidente em teste real (2026-07-19):** ao abrir a tela de SD pelo hardware
de verdade, `taskLVGL` travou dentro de `build_list()` criando os botões da
lista (watchdog disparando, heap parado). Duas hipóteses investigadas:
1. `s_entry_count` maior que o array `s_entries[48]` — corrigido
   (`app_bcu_sd_async_get_dir_result` devolvia contagem não limitada; e
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
`app_bcu_sniffer_start()` (chamado por `ui_screen_can_sniffer_show()` e pelo
botão START/STOP) ainda chama `app_bcu_sd_mount()` de forma síncrona. Na
prática só bloqueia de verdade se o cartão **não** tiver sido montado no
boot (ex: cartão inserido depois) — o caso comum (já montado) retorna na
hora. Se isso incomodar, dá pra trocar essas duas chamadas por
`app_bcu_sd_async_mount()` seguindo o mesmo padrão.

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
em `app_bcu.c`). Precisa trocar `TWAI_MODE_LISTEN_ONLY` → `TWAI_MODE_NORMAL`
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

Isso é **independente** da ECU programável — é uma segunda fonte de dados (o
carro de fábrica), não conflita com o app_ecu.

**Implementado**: `app_bcu_obd2_set_active()`/`app_bcu_obd2_request_pid()`
em `components/app_bcu/` — reinstala o driver TWAI em `TWAI_MODE_NORMAL`
(transmite) quando ligado, volta pro `TWAI_MODE_LISTEN_ONLY` padrão quando
desligado. **Desligado por padrão** — precisa ser ligado explicitamente
(botão "Ativar OBD2" na aba Decoder da tela CAN, ou o botão "CAN" do
dashboard); até lá o painel continua só escutando, igual ao sniffer. Não
testado em hardware real ainda (precisa do Vectra com ignição ligada
respondendo Mode 01).

**O round robin e a decodificação moram no `app_bcu`, não na UI** (mudou
nesta rodada — antes era um `lv_timer` dentro do `ui_screen_can.c`). Com o
modo ativo, a própria task de captura pede um PID a cada 150ms e decodifica
as respostas de `0x7E8` num snapshot (`app_bcu_obd2_get_data()`, struct
`app_bcu_obd2_data_t`). Três consequências que motivaram a mudança:
1. O dado continua vivo com a tela do CAN **fechada** — é o que permite o
   dashboard consumir OBD2 (§10).
2. Um único round robin no barramento, não um por tela aberta.
3. `valid` cai sozinho depois de `APP_BCU_OBD2_STALE_MS` (2s) sem resposta,
   em vez de congelar o último valor lido como se ainda fosse atual.

A recepção agora roda com o sniffer parado, se o OBD2 estiver ativo — a
task usa o gate `s_rx_paused || (!s_running && !s_obd2_active)`. Com só o
OBD2 ligado, a tabela por ID e os contadores do sniffer **não** são
alimentados (não faria sentido inflá-los sem sessão de captura), mas a
resposta `0x7E8` é decodificada **antes** do filtro de software do sniffer
— o filtro é sobre o que se quer logar, não pode calar a fonte do
dashboard.

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

`ui_screen_dashboard.c` ganhou (ao longo de duas rodadas — a rodada de
2026-07-21 já tinha o essencial, uma rodada depois adicionou a cor por
perfil em cima disso):
- **4 modelos de tela** (`app_dash_layout_t`: Classic/Race/Grid/Duplo), cada
  um com seu próprio builder (`build_classic_layout`/`build_race_layout`/
  `build_grid_layout`/`build_twin_layout`), escolhidos por
  `switch (s_active_profile.layout)` em `ui_screen_dashboard_show()`. Grid é
  o único que usa `lv_arc` (com "knob" fazendo de ponteiro); os outros usam
  mostradores `lv_scale` de verdade (`build_dial()`) com ponteiro-linha
  (`lv_scale_set_line_needle_value`) — visual de tacômetro/velocímetro
  clássico.
- **Configuração única e global** (não existe mais tela/modal de "Perfis"
  com lista + "Salvar como novo" dentro do dashboard): Estilo, Modelo de
  tela, Corte (RPM) e Cor do acento se editam na tela de Configurações
  (`ui_screen_config.c`, seção "DASHBOARD"), aplicando na hora e persistindo
  sozinho — sempre lê/grava o perfil "ativo" de `app_dash_profile`
  (`APP_DASH_PROFILE_MAX`=8 continua existindo na API, mas a UI só expõe um
  perfil por vez).
- **Cor do acento do RPM** (`ui_dash_accent_color()`/`ui_dash_accent_name()`
  em `ui_screen_dashboard.c`, declaradas em `ui.h` pra tela de Config poder
  montar o dropdown "Cor"): 6 cores fixas (Azul/Verde/Amarelo/Vermelho/Roxo/
  Branco). Só tem efeito visível no layout **Grid** — é o único com o `lv_arc`
  colorível; os outros layouts usam `lv_scale`, que não tem um indicador
  separado pra pintar.
- Separado disso, `zotti_theme.c`/`.h` implementa um **tema global de app**
  (TEMA1/2/3 — azul original / esportivo preto-vermelho / clássico âmbar),
  trocável no dropdown "Tema" da tela de Config, persistido em NVS,
  aplicando em `ZOTTI_BG`/`ZOTTI_ACCENT`/etc via variáveis globais (as telas
  não precisaram mudar — só passaram a ler uma variável em vez de uma
  constante). Independente da cor do acento do RPM acima — um é o esquema de
  cor de toda a UI, o outro é só o destaque do mostrador de RPM no Grid.
- **"Brilho"** (`zotti_brightness.c`/`.h`, slider na seção DISPLAY da tela
  de Config): ⚠️ **não é backlight PWM real** — este hardware não tem essa
  fiação (`bsp_backlight_set()` é liga/desliga via CH422G, mesmo expansor
  I2C do SD/touch/CAN select, não um pino PWM do ESP32; confirmado, não
  existe outro caminho de dimming neste board). É esmaecimento por
  software: uma superposição preta translúcida no `lv_layer_top()` da
  LVGL (por cima de qualquer tela, sem mexer em cada uma), opacidade
  inversamente proporcional ao valor escolhido, nunca preto total (piso
  em 20% de "brilho" limita a opacidade máxima da superposição, senão
  pareceria tela travada/desligada). Não desliga LED nenhum — consumo do
  backlight continua o mesmo independente do valor. Persistido em NVS,
  padrão de fábrica reduzido pra 70% (pedido explícito: "baixar o brilho"
  já de cara, não só deixar a opção disponível).
- **Efeito de corte**: quando RPM ≥ 90% do redline do perfil ativo, o número
  do RPM (e o arco, no Grid) piscam entre branco e vermelho (`lv_anim`, com
  histerese em 85% pra não ficar oscilando na borda).
- **Animação suave do ponteiro**: o arco (Grid) anima entre o valor antigo e
  o novo (180ms) em vez de saltar — `animate_arc_to()`.
- Cartões de sensor hoje mostram TPS/ECT/BATERIA (AFR/IAT saíram — decisão
  já tomada antes desta sessão, não mexi nisso).

**Fonte de dados do dashboard** — o cabeçalho tem dois botões à direita
(`dash_src_t` em `ui_screen_dashboard.c`), e as três fontes são mutuamente
exclusivas (um enum, não dois booleanos soltos — não existe "Demo e CAN ao
mesmo tempo"):

| Botão | Fonte | Componente | Escuta ou fala? |
|---|---|---|---|
| nenhum aceso | ECU programável por BLE (padrão) | `app_ecu` | só escuta |
| "Demo" (amarelo) | simulador local | `app_sim` | nada no barramento |
| "CAN" (vermelho) | OBD2 Mode 01 do carro de fábrica | `app_bcu` (§6) | **transmite** |

O botão CAN é vermelho de propósito: é a única fonte em que o painel
**transmite** no barramento (driver TWAI em `NORMAL`). Sair do modo CAN
devolve o TWAI pro `LISTEN_ONLY`. Diferente do BLE da ECU, o OBD2 traz
velocidade de verdade (PID `0x0D`); em compensação, esta tabela de PIDs não
tem lambda/AFR nem aceleração — vão zerados na `ui_screen_dashboard_update()`.

Como `app_bcu`/`app_sim` também são ligados por **outras** telas (botão OBD2
da tela CAN, botão Demo da tela da ECU), `s_source` é só uma preferência: a
cada `ui_screen_dashboard_show()` ela é reconciliada com o estado real dos
componentes (OBD2 ativo ganha; senão segue `app_sim_is_enabled()`). Sem
isso, um botão daqui ficava aceso apontando pra uma fonte que já não existia.

⚠️ `app_bcu_obd2_set_active()` reinstala o driver TWAI e espera 100ms —
chamado da task do LVGL, trava a tela por esse tempo. Aceitável num toque
deliberado de botão (é o mesmo custo que o botão da tela do CAN sempre
pagou), mas **não** chame isso de dentro do timer de 33ms do dashboard.

**Modo Demo** (`components/app_sim/`, completamente separado de
`app_ecu`/`app_bcu` de propósito): liga
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

- Telas com stub/gap conhecidos e não tocados: `app_inputs.c`/`app_outputs.c`
  (placeholders), TODOs em `ui_screen_config.c`.
- Scanner ELM327 via BLE (protocolo não definido — diferente do OBD2 direto
  sobre CAN do §6, que já foi implementado): a tela placeholder que existia
  pra isso (`ui_screen_scanner.c`, menu "Scanner") foi **substituída** pela
  tela "Computador de Bordo" (`ui_screen_bcu_trip.c`, ver §2) — não há mais
  nenhum placeholder reservado pro scanner ELM327. Se essa ideia voltar,
  precisa de uma tela nova do zero.
- Aba "Gateway" da tela CAN (`ui_screen_can.c`) continua placeholder —
  fora do escopo do §6, roadmap não detalha o que ela deveria fazer.
- `app_dash_profile` ainda suporta múltiplos perfis por índice na API
  (`APP_DASH_PROFILE_MAX`=8), mas desde a config única (§10) nenhuma tela
  cria perfil novo — só edita o ativo. Se algum dia isso voltar a fazer
  sentido (ex: perfil por piloto), a UI pra criar/nomear um novo perfil
  precisaria ser reconstruída (foi removida junto com o modal antigo).

## 9. Próximos passos (em ordem sugerida)

1. **Confirmar a pendência de pino do §5** antes de ligar qualquer fio físico
   no módulo de pedal — bloqueia fisicamente o resto do módulo de pedal (e
   com isso, testar `ui_screen_pedal.c` com o link de verdade).
2. Decidir e implementar o GATT client BLE em `app_ble.c`/`app_ecu.c` assim
   que o firmware da ECU definir o UUID do serviço/characteristic (§4).
3. Retestar em hardware se o travamento do SD (§3) foi realmente resolvido.
4. Testar o OBD2 ativo (§6) num carro de verdade — implementado mas nunca
   rodou contra um Vectra respondendo Mode 01 de fato. Agora dá pra testar
   por dois caminhos: a aba Decoder da tela CAN (valores crus, PID a PID) e
   o botão "CAN" do dashboard (§10). Vale conferir os dois, porque o
   dashboard exercita o snapshot com a tela do CAN fechada.
5. Projetar o hardware do módulo de pedal (schematic + PCB) com os chips
   automotivos de referência (§7) — H-bridge DRV8873-Q1 primeiro, por ser
   reaproveitável.
6. Opcional: scanner ELM327 via BLE (§12) — precisa definir o protocolo e
   desenhar uma tela nova do zero (a antiga foi substituída pelo Computador
   de Bordo), diferente do OBD2 direto sobre CAN que já foi implementado.
7. Implementar o GATT **server** de escrita no firmware da ECU (STM32) e o
   GATT **client** de escrita correspondente em `app_ble.c`/`app_map.c` (o
   painel) pro protocolo do §13 funcionar de ponta a ponta — hoje só o
   framing/CRC existe e é testado, nada é transmitido de verdade ainda.

## 13. Mapas de tunagem (injeção/ignição/sonda) — exceção à regra de ouro do §1

Decisão explícita tomada em sessão de 2026-08-31, não um acidente de
arquitetura (a "regra de ouro" do §1 exige exatamente isso: qualquer
mudança precisa ser revisada e registrada, não silenciosa).

**O que muda:** até aqui o painel só assinava notificações da ECU (BLE
notify, leitura). A partir de `components/app_map/` isso deixa de ser
verdade só para os MAPAS de tunagem — três tabelas RPM x carga (MAP em
kPa), mesmo conceito de VE table/ignition table/AFR target table do
Speeduino/MegaSquirt/rusEFI já citados no §7:
- **Injeção** — tempo de injeção (décimos de ms).
- **Ignição** — avanço/retardo (décimos de grau, com sinal).
- **Sonda** — lambda ALVO pra malha fechada (centésimos de lambda; ex.:
  85 = 0.85, rico sob carga alta). Não confundir com o campo
  `lambda`/`lambda_x1000` do `app_ecu` (§4), que é a leitura AO VIVO da
  sonda — este mapa é o alvo que a ECU persegue, não uma leitura.

O painel passa a poder **escrever** um mapa novo na ECU. O protocolo (2ª
versão, 2026-09-01) não é mais um framing inventado do zero — é baseado no
**UDS (ISO 14229-1)**, o protocolo automotivo real pra exatamente isto
(escrever um bloco de calibração na memória não-volátil de uma ECU), com
Service IDs, subfunções e códigos de erro (NRC) reais do padrão,
pesquisados e documentados byte a byte em
[`components/app_map/include/app_map.h`](components/app_map/include/app_map.h).
Sequência: `DiagnosticSessionControl(programmingSession)` →
`SecurityAccess` (seed/key) → `RequestDownload` → `TransferData` (repetido)
→ `RequestTransferExit`. **Por que UDS e não XCP** (o outro padrão
automotivo candidato, ASAM MCD-1 XCP): XCP é pra tunagem AO VIVO com motor
rodando; este projeto decidiu o oposto (só grava com motor parado, ver
abaixo) — isso é exatamente o caso de uso do fluxo
RequestDownload/TransferData/RequestTransferExit do UDS (reflash de
calibração em modo de serviço). A escolha de protocolo seguiu a decisão de
segurança já tomada, não o contrário. Telemetria (§4) e OBD2 (§6)
continuam **só leitura**, sem mudança nenhuma — a exceção é só para este
dado de calibração.

**Como a segurança foi resolvida (decisão do usuário, não my default):**
a ECU (firmware STM32, fora deste repo) é quem tem a palavra final — ela
DEVE validar/clampar os valores recebidos contra limites físicos e
**recusar gravar um mapa novo com o motor girando** (RPM > 0), só aceita
com o motor parado. No protocolo UDS isso é o NRC padrão `0x22
conditionsNotCorrect`, respondido já na entrada da sessão de programação
(ponto principal de recusa) e de novo no `RequestTransferExit` (caso o
motor tenha ligado no meio da transferência). Além disso agora existe uma
camada de `SecurityAccess` (seed/key) antes de aceitar qualquer escrita —
⚠️ o algoritmo seed→key implementado é uma transformação simples DE
DEMONSTRAÇÃO (rotação de bits + XOR), não criptografia real; o objetivo é
recusar escrita de um app BLE genérico por acidente, não resistir a um
atacante que capturou o tráfego (ver aviso grande em `app_map.h`). Essa
validação toda mora inteiramente no firmware da ECU — não tem como o
painel garantir isso remotamente, só pode confiar na resposta.

**A ECU é a fonte de verdade, não o painel (decisão de 2026-09-01):** até
aqui a tela só lia o cache local (NVS) ao abrir — o painel podia mostrar
um mapa desatualizado sem avisar. Corrigido: `ui_screen_map_show()` agora
tenta **ler o mapa atual da ECU primeiro** (`app_map_read_from_ecu()`, via
`RequestUpload` 0x35 — o par de leitura do `RequestDownload`, mesmo
formato, sem exigir sessão de programação nem `SecurityAccess` porque ler
é mais leve que escrever) antes de deixar editar qualquer coisa. Só cai
pro cache local em NVS se a ECU não responder (hoje sempre, mesma
pendência do GATT client), e a barra de status da tela avisa claramente
qual dos dois está mostrando — nunca finge que o cache local é dado
confirmado. Diferença de framing do UDS real entre download e upload: no
upload quem carrega o dado é a **resposta** do `TransferData`
(`[0x76][BSC][dado]`), não o pedido (`[0x36][BSC]`, vazio) — documentado
em `app_map.h`.

**Estado atual (lado painel, este repo):**
- Editor completo na tela "Mapas" (`ui_screen_map.c`) — grade heatmap
  colorida (estilo FuelTech/Injepro) por RPM x kPa, uma aba por tabela,
  toque pra selecionar célula, passos, só grava ao clicar "Salvar Mapa"
  (nada persiste ao simplesmente editar). No máximo UMA aba tem sua grade
  montada em LVGL por vez — trocar de aba destrói a grade da anterior e
  constrói a nova (valores continuam intactos em `s_set`, só a UI é
  recriada). Ver aviso grande no topo de `ui_screen_map.c`: dois
  travamentos/reboots reais em hardware no mesmo dia (2026-08-31), os dois
  pelo mesmo motivo raiz — o pool FIXO de 64KB da LVGL
  (`CONFIG_LV_MEM_SIZE_KILOBYTES`, à parte dos 8MB de PSRAM da placa).
  1º: célula = 2 objetos, as 2 abas construídas de uma vez de saída —
  corrigido pra 1 objeto/célula + só construir a aba ativa. 2º: essa
  primeira correção só adiava o estouro — trocar de aba ia SOMANDO grades
  (nunca destruía a antiga), então visitar as 3 abas reproduzia o mesmo
  estouro; corrigido destruindo a grade da aba anterior a cada troca.
- Curva padrão (`app_map_reset_default`) garantidamente **sem quebra**
  dentro da grade real — as fórmulas são lineares e as constantes foram
  escolhidas pra nunca saturar o clamp MIN/MAX (o que criaria um "kink" de
  inclinação); teste automatizado em `test_app_map.c` trava se um ajuste
  futuro nas constantes voltar a saturar.
- Cache local em NVS (`app_map_save_local`/`app_map_get`) — só entra como
  QUEDA quando a ECU não responde (ver acima); nunca é a cópia que manda
  de verdade (essa é a da flash da ECU).
- Protocolo UDS (montagem de cada PDU: sessão, seed/key, RequestDownload,
  TransferData, RequestTransferExit, e a decodificação de resposta
  positiva/negativa) implementado e testado via Unity
  (`test_app/main/test_app_map.c`) — cobre cada PDU byte a byte, o
  encadeamento blockSequenceCounter, reassemblagem contra
  `app_map_serialize_table` e o CRC16 contra vetor de teste padrão.
- `app_map_send_to_ecu()` (escrita) e `app_map_read_from_ecu()` (leitura,
  novo) são stubs honestos: cada PDU das duas sequências já pode ser
  montado e decodificado de verdade (testável) mas as funções retornam
  `ESP_ERR_NOT_SUPPORTED` porque falta o GATT client em `app_ble.c` —
  mesma pendência que o subscribe de telemetria do §4 já tinha antes de
  existir a ECU de verdade.

**Falta (lado ECU, fora deste repo):** o firmware STM32WB5MM-DK precisa
implementar o GATT **server** com characteristics de leitura E escrita pro
protocolo UDS de `app_map.h`, decodificar cada serviço
(0x10/0x27/0x34/0x35/0x36/0x37), validar o CRC16 e a faixa de cada célula,
aplicar a trava de "motor parado" (só na escrita — leitura não é gated),
gravar na flash interna, e reler sozinho no boot. ⚠️
[`STM32_ECU_SIMULADOR_BLE.md`](STM32_ECU_SIMULADOR_BLE.md) (guia passo a
passo — STM32CubeIDE, protocolo byte a byte, pinos/hardware do simulador
por potenciômetro, código de referência, escrito pra ser colado numa
sessão de IA separada trabalhando no projeto STM32) ainda documenta só o
`RequestDownload` (escrita) — o `RequestUpload` (leitura, 0x35) é novo
desta sessão e o guia ainda não foi atualizado com ele; avisar
explicitamente antes de colar numa sessão de firmware, ou pedir pra
regenerar o guia primeiro.

**Ferramenta de debug TEMPORÁRIA — `components/app_map_debug_ble/`
(2026-09-01):** pedida explicitamente pra dar pra inspecionar via nRF
Connect (app de celular) os bytes que a tela "Mapas" mandaria, sem
precisar esperar o firmware da ECU existir. Liga o papel de PERIFÉRICO
BLE no próprio painel (hoje ele só faz papel de central/scanner) —
anuncia como **"ZOTTI-ECU"** (é esse o nome pra filtrar no nRF Connect;
mesmo nome já configurado em `app_ble.c`), com um serviço GATT usando os
mesmos UUIDs do guia STM32. Um "sniffer" registrado em `app_map.c`
(`app_map_set_debug_sniffer`) notifica cada PDU que `app_map_send_to_ecu()`
montaria, na ordem, toda vez que "Salvar Mapa" é clicado com um celular
conectado e inscrito na characteristic — custo zero quando ninguém está
conectado. Ver o comentário grande em
[`components/app_map_debug_ble/include/app_map_debug_ble.h`](components/app_map_debug_ble/include/app_map_debug_ble.h)
pro passo a passo de uso E de remoção (é só apagar a pasta + 2 chamadas
marcadas "DEBUG TEMPORARIO" em `app_ble.c` + 1 linha do `CMakeLists.txt`
dele — não é destinado a virar parte do produto final).
