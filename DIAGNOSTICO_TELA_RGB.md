# Diagnostico da instabilidade visual do LCD

## Sintoma

A imagem apresentava tremida/deslocamento ao entrar em menus e no Dashboard,
mais perceptivel durante o Demo. O problema nao era uma falha do touch nem do
cartao SD: o painel RGB continua fazendo scanout por DMA independentemente da
tela ou do dado exibido.

## Causa encontrada

O firmware usava timings horizontais genericos (`front=8`, `pulse=4`,
`back=8`). Eles nao correspondem ao painel RGB da Waveshare ESP32-S3
Touch-LCD-4.3B. Com uma janela horizontal curta, o controlador RGB perde a
margem de sincronismo quando LVGL atualiza uma tela inteira; o resultado e um
quadro deslocado ou visualmente embaralhado.

O boot tambem mostrou que o projeto ainda rodava a CPU 160 MHz e PSRAM/flash
80 MHz. Isso aconteceu porque o `sdkconfig` ja existente preservava os valores
antigos, mesmo apos a edicao de `sdkconfig.defaults`. A menor margem de CPU e
barramento agravava o problema durante as atualizacoes do Dashboard.

## Correcao aplicada

- Timing nativo do painel: PCLK 16 MHz, borda negativa, H `40/48/88` e V
  `13/3/32` (front/pulse/back).
- Tres framebuffers em PSRAM e bounce buffer de 40 linhas para separar o
  scanout do desenho do LVGL.
- Reinicio RGB no VSync, evitando deslocar a imagem no meio do quadro.
- CPU 240 MHz, flash e PSRAM 120 MHz, cache de dados de 64 bytes e FreeRTOS
  em 1 kHz.
- Dashboard e simulador atualizados a aproximadamente 30 Hz; antes o timer
  do Dashboard era de 200 ms, impondo somente 5 atualizacoes por segundo.

## Como validar

Depois de compilar e gravar, o boot precisa registrar CPU em 240 MHz e PSRAM
em 120 MHz. A navegacao deve permanecer estavel e o Demo deve exibir
`DEMO ATIVO` no cabecalho, com `APP_SIM: Demo ativado` no monitor serial.

## Cartao SD

O SD nao causa a tremida. Em um boot ele montou e abriu um CSV; em outro,
retornou CRC invalido (`0x109`). Para esse segundo caso o clock SPI do cartao
foi reduzido para 10 MHz, privilegiando confiabilidade.
