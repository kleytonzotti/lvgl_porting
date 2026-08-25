# LVGL Porting — Waveshare ESP32-S3-Touch-LCD-4.3B

Porting do LVGL 9 para o display Waveshare ESP32-S3-Touch-LCD-4.3B (módulo ESP32-S3-N16R8).

> Arquitetura do sistema completo (ECU programável, módulo de pedal, protocolos BLE/UART, hardware de referência): ver [ROADMAP.md](ROADMAP.md).

## Hardware

- Placa: Waveshare ESP32-S3-Touch-LCD-4.3B
- Módulo: ESP32-S3-N16R8 — 16MB Flash + 8MB PSRAM **Octal**
- Touch: GT911

## Toolchain

- ESP-IDF: **v5.5.4** (target `esp32s3`)
- Build/flash feito pela extensão ESP-IDF do VSCode (`idf.currentSetup` em [.vscode/settings.json](.vscode/settings.json)); `idf.py` não está disponível como CLI solto neste ambiente — use sempre os comandos da extensão (Reconfigure / Build / Flash / Monitor).
- Porta serial: configurada em `idf.port` (`.vscode/settings.json`) — ajuste para a porta do seu SO (`/dev/ttyACM0` no Linux, `COMx` no Windows).

## Configuração obrigatória (sdkconfig)

Toda a configuração necessária já está em [sdkconfig.defaults](sdkconfig.defaults) e é aplicada automaticamente ao gerar o `sdkconfig`:

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y

CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

## Estabilidade do LCD e FPS

- O painel RGB usa tres framebuffers em PSRAM, bounce buffer de 40 linhas em
  SRAM e `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`. Esta ultima opcao faz qualquer
  reinicio de transmissao provocado por underflow ocorrer no VSync, evitando
  o deslocamento horizontal visivel no meio de um quadro durante trocas de
  tela ou no modo Demo.
- A cadencia alvo do LVGL e **30 FPS** (`BSP_LVGL_TARGET_FPS` em
  `components/bsp_waveshare_43/include/bsp_waveshare_43.h`). O valor medido,
  que pode ser menor sob carga, aparece no canto inferior esquerdo de todas
  as telas. Nao reduza esse valor para mascarar problemas de DMA: use o
  indicador e os logs `[FLUSH]` para diagnosticar queda real de desempenho.
- Os timings RGB usam PCLK de 16 MHz, borda negativa, porches H 40/48/88 e
  V 13/3/32 — valores especificos do painel 4.3B. CPU, flash e PSRAM devem
  operar em 240 MHz/120 MHz/120 MHz; o boot precisa mostrar `cpu freq:
  240000000 Hz` e `PSRAM: Speed: 120MHz`. Nao altere apenas o FPS do LVGL
  esperando corrigir tremida de sinal.
- Depois de alterar `sdkconfig.defaults`, recrie o `sdkconfig` conforme o
  procedimento abaixo; configuracoes ja gravadas nao sao substituidas por
  uma simples reconfiguracao.

**Por quê essas duas seções precisam estar sempre alinhadas:** no ESP32-S3, flash e PSRAM compartilham o mesmo clock do MSPI (`CONFIG_SOC_MEMSPI_CORE_CLK_SHARED_WITH_PSRAM`). PSRAM em Quad/40MHz, ou qualquer combinação onde a PSRAM esteja em Octal/80MHz mas o flash não acompanhe (ex: flash ainda em DIO/40MHz), corrompe o framebuffer — o painel RGB faz DMA contínuo direto da PSRAM, então qualquer inconsistência de timing nesse barramento aparece como imagem embaralhada na tela. Já aconteceu nas duas formas (PSRAM errada e depois flash errado) durante o desenvolvimento.

**Importante ao alterar `sdkconfig.defaults`:** o arquivo `sdkconfig` (gerado, fora do git) só recebe valores de `sdkconfig.defaults` para símbolos que **ainda não têm valor salvo**. Se você mudar `sdkconfig.defaults` e rodar apenas "Reconfigure Project", valores antigos já presentes no `sdkconfig` (ex: modo Quad, flash DIO) **não são sobrescritos**. Para garantir que a mudança realmente é aplicada:

1. Apague o `sdkconfig` local.
2. Rode **ESP-IDF: Reconfigure Project** (ou abra e salve o SDK Configuration Editor) para gerar um `sdkconfig` novo a partir do `sdkconfig.defaults`.
3. Confira que `CONFIG_SPIRAM_MODE_OCT`, `CONFIG_SPIRAM_SPEED_80M`, `CONFIG_ESPTOOLPY_FLASHMODE_QIO` e `CONFIG_ESPTOOLPY_FLASHFREQ_80M` estão realmente marcados no novo `sdkconfig`.
4. **Build** e depois **Flash** (reconfigurar sozinho não gera novo binário nem regrava a placa).

## Partição

- Tabela de partição single-app custom: `partitions_singleapp.csv`, offset `0x8000` (ver `CONFIG_PARTITION_TABLE_*` no `sdkconfig`).

## LVGL / Fontes

- Fonte padrão do LVGL: Montserrat 14 (`CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14`).
- Fontes customizadas do projeto (além das do Kconfig do LVGL) ficam em [components/ui/include/zotti_fonts.h](components/ui/include/zotti_fonts.h).

## Dependências (managed components)

Ver [main/idf_component.yml](main/idf_component.yml):
- `lvgl/lvgl` `^9.3.0`
- `espressif/esp_lvgl_port` `^2.0.0`
- `espressif/esp_lcd_touch_gt911` `>=1.0.0`
- `espressif/esp_lcd_touch` `>=1.0.0`
