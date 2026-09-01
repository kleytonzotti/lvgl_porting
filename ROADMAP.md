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
| `test_app/` **(novo)** | App de teste Unity separado (`idf.py -C test_app build flash monitor`) | Cobre app_ecu, app_pedal_link (parser), app_sim |

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
