#ifndef APP_MAP_DEBUG_BLE_H
#define APP_MAP_DEBUG_BLE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────
// FERRAMENTA DE DEBUG TEMPORÁRIA — não faz parte do produto final, pedida
// explicitamente (2026-09-01) pra dar pra inspecionar via nRF Connect (app
// de celular) os bytes que a tela "Mapas" mandaria pra uma ECU real, sem
// precisar do hardware STM32 existir ainda.
//
// O QUE FAZ: liga o papel de PERIFÉRICO BLE no próprio painel (hoje o
// painel só faz papel de CENTRAL/scanner, ver components/app_ble/) —
// anuncia como "ZOTTI-ECU" (mesmo nome já configurado em app_ble.c) com um
// serviço GATT usando os MESMOS UUIDs documentados em
// STM32_ECU_SIMULADOR_BLE.md, e registra um "sniffer" em app_map.c
// (app_map_set_debug_sniffer) que notifica, PDU a PDU, a sequência
// INTEIRA que app_map_send_to_ecu() montaria pra escrever uma tabela —
// sessão, segurança, RequestDownload, cada TransferData,
// RequestTransferExit.
//
// COMO USAR (opção simples, sem precisar do BLE funcionar): abra o monitor
// serial do painel, vá na tela Mapas e clique "Salvar Mapa" — cada PDU
// aparece direto no log como "[DEBUG-BLE] PDU (len=N): AA BB CC ...".
//
// COMO USAR (via nRF Connect, se quiser testar o transporte BLE de
// verdade): no celular, abra o nRF Connect → escaneie → conecte no
// dispositivo "ZOTTI-ECU" → entre no serviço
// 7a2b1000-ec00-4a5d-9f6b-1234567890ab → ative notificações (ícone de
// seta pra baixo) na characteristic 7a2b1002-ec00-4a5d-9f6b-1234567890ab
// ("UDS Request") → na tela Mapas do painel, clique "Salvar Mapa" — cada
// PDU aparece no log do nRF Connect em hexadecimal, na ordem que seria
// transmitida de verdade. Se o nRF Connect não mostrar nada mesmo depois
// de conectado, confira se a notificação foi realmente ativada: o log
// serial do painel mostra "[DEBUG-BLE] subscribe: attr=... reason=..."
// no exato momento em que o celular ativa — se essa linha nunca aparecer,
// o app não chegou a ativar a notificação (varia de ícone conforme a
// versão do nRF Connect).
//
// COMO REMOVER (quando não precisar mais):
//   1. Apagar esta pasta inteira (components/app_map_debug_ble/).
//   2. Tirar as 2 chamadas marcadas "DEBUG TEMPORÁRIO" em
//      components/app_ble/src/app_ble.c (e o #include deste header lá).
//   3. Tirar "app_map_debug_ble" do REQUIRES de
//      components/app_ble/CMakeLists.txt.
//   4. Opcional (pode deixar): app_map_set_debug_sniffer() em
//      app_map.h/.c — sozinha ela não faz nada, só existe pra alguém
//      poder se inscrever; não precisa remover pra "limpar" o debug.
// ─────────────────────────────────────────────────────

// Registra o serviço GATT de debug — chamar UMA VEZ, ANTES do host NimBLE
// começar a rodar (antes de nimble_port_freertos_init() em app_ble.c).
esp_err_t app_map_debug_ble_register_gatt(void);

// Começa (ou reinicia, após desconexão) o anúncio BLE como "ZOTTI-ECU" —
// chamar de dentro do callback de sincronização do host (ble_on_sync em
// app_ble.c), depois que o host NimBLE já estiver pronto.
void app_map_debug_ble_start_advertising(void);

#ifdef __cplusplus
}
#endif

#endif
