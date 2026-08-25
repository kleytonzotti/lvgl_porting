# Relatorio de revisao tecnica

Data da revisao: 2026-07-27.

## Correcoes aplicadas

1. **Chacoalhada/deslocamento horizontal do LCD RGB.** O driver podia
   reiniciar a transmissao logo que detectasse atraso no abastecimento do
   bounce buffer. Se isso acontecia no meio do quadro, a imagem era exibida
   deslocada por um frame, sobretudo durante a criacao de menus e no Demo.
   Foi habilitado `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`, para que o reinicio
   seja sincronizado ao VSync. O painel agora usa tres framebuffers em PSRAM
   (requisito do `avoid_tearing` do port) e bounce buffer de 40 linhas, que
   da mais tempo para o DMA ser realimentado sob carga.
2. **FPS artificialmente baixo.** A taxa estava limitada a 10 FPS. Foi
   ajustada para 30 FPS, dentro da margem do painel com PCLK de 16 MHz
   (aprox. 39 Hz teoricos com os timings atuais). O FPS renderizado real e
   mostrado no canto inferior esquerdo em todas as telas.
3. **Concorrencia no modo Demo.** O estado ligado/desligado e o redline eram
   acessados de duas tasks sem a mesma protecao usada pelo snapshot de dados.
   Todas as APIs publicas de leitura/escrita agora usam o mutex do simulador.
4. **Concorrencia nos minimos/maximos.** A task LVGL atualizava a estrutura
   enquanto o `esp_timer` podia copia-la e grava-la na NVS. Foi adicionado
   mutex e a gravacao recebe um snapshot consistente, fora da secao critica.
5. **Timer BLE orfao.** Ao sair de Configuracoes com o popup BLE aberto, o
   timer podia continuar atualizando objetos ja destruidos. O callback de
   exclusao da tela cancela scan/timer e limpa as referencias.
6. **NVS inicializada tarde demais.** O log de boot mostrava o Bluetooth
   iniciando antes de `nvs_flash_init`, impedindo a leitura/gravação da
   calibração PHY. A inicializacao foi centralizada no inicio de `app_core`.
7. **Timing RGB incorreto e clock insuficiente.** Os porches originais eram
   genericos e nao correspondiam ao painel. Foram substituidos pelos valores
   usados em implementacao funcional da 4.3B: PCLK 16 MHz, borda negativa,
   H 40/48/88 e V 13/3/32. Tambem foram definidos CPU a 240 MHz e flash/PSRAM
   a 120 MHz, com linha de cache de 64 bytes, conforme recomendacao da
   Waveshare para este painel.

## Verificacao da implementacao do roadmap

Implementados no repositorio: CAN passivo e logger SD assincrono, BLE scan e
connect, parser de telemetria ECU, parser UART do pedal, simulador, perfis e
min/max do dashboard, quatro layouts de dashboard, OBD2 sob ativacao
explicita e testes Unity para ECU/pedal/simulador.

Pendencias declaradas no roadmap nao foram implementadas por dependerem de
definicao externa, e nao devem ser inventadas no firmware:

- Cliente GATT da ECU: faltam UUIDs de servico e characteristic.
- Link RS485 do pedal: GPIO 43/44 conflitam com o console da placa; exige
  confirmacao de esquematico/pinos antes de habilitar a UART.
- Validacao em hardware do SD e do OBD2 no Vectra.
- Scanner ELM327 BLE, Gateway CAN, entradas/saidas e OTA: ainda sao stubs;
  faltam requisitos/protocolo para uma implementacao segura.

## Melhorias recomendadas

- Fazer flash com `sdkconfig` recriado e testar navegacao intensa e Demo por
  pelo menos 10 minutos. Registrar os logs `[FLUSH]`, `DIAG-LVGL-TASK` e o
  FPS exibido. Caso ainda haja underflow, reduzir PCLK para 14 MHz e comparar
  o resultado; nao baixar o FPS como paliativo.
- O commit em NVS ocorre a cada tres segundos quando ha recorde novo. Apesar
  de estar fora da task LVGL, escrita em flash ainda pode reduzir a margem do
  barramento. Se os logs coincidirem com as quedas, acumular os recordes por
  mais tempo ou gravar apenas ao sair do dashboard.
- Acrescentar testes de integracao no hardware para transicao de telas,
  navegacao com popup BLE e monitoramento de heap/stack. Os testes Unity
  atuais nao exercitam o LCD/LVGL.

## Limitacao da revisao

A compilacao e a verificacao estatica podem ser executadas localmente, mas a
eliminacao definitiva do artefato RGB exige flash e observacao na placa. A
correcao usa o mecanismo previsto pelo driver ESP-IDF e preserva os
diagnosticos necessarios para confirmar o resultado em hardware.
