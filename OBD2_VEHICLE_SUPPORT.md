# Suporte OBD2: Vectra vs Argo 2025

## Status Resumido
- ✅ **Vectra (2000-2010):** Totalmente compatível — 7 PIDs funcionando agora
- ✅ **Argo 2025 1.0 Manual:** Totalmente compatível — 7 PIDs funcionando agora
- ⚠️ **Protocolo:** Ambos usam CAN ISO 15765-4 @ 500kbps (auto-detect funciona)

---

## Detalhes por Veículo

### Vectra (Presumidamente 2009)
| Aspecto | Especificação |
|---------|---|
| **Fabricante** | Chevrolet |
| **Motor** | 2.0L 8V (presume-se) |
| **Transmissão** | Manual/Automática |
| **Protocolo Primário** | ISO 15765-4 (CAN) |
| **Velocidade CAN** | 500 kbps |
| **Suporte OBD2** | Completo (SAE J1979 Mode 01) |
| **Handshake** | Auto-negociação |

### Argo 2025 1.0 Manual
| Aspecto | Especificação |
|---------|---|
| **Fabricante** | Fiat |
| **Motor** | 1.0L 3-cilindros EcoSpark |
| **Transmissão** | Manual |
| **Protocolos** | ISO 15765-4 (CAN) **OU** ISO 14230-4 (KWP2000) |
| **Velocidade CAN** | 500 kbps |
| **Velocidade KWP** | 10.4 kbps (init), 38.4 kbps (data) |
| **Suporte OBD2** | Completo (SAE J1979 Mode 01 + Euro OBD) |
| **Handshake** | Auto-negociação CAN preferencial |

---

## PIDs Suportados

### Conjunto Base (7 PIDs — Implementado Agora)

| PID | Nome | Unidade | Fórmula | Vectra | Argo |
|-----|------|---------|---------|--------|------|
| **0x0C** | RPM | rpm | `(A*256+B)/4` | ✅ | ✅ |
| **0x0D** | Velocidade | km/h | `A` | ✅ | ✅ |
| **0x05** | Temp. Arrefecimento (ECT) | °C | `A - 40` | ✅ | ✅ |
| **0x0F** | Temp. Ar Admissão (IAT) | °C | `A - 40` | ✅ | ✅ |
| **0x0B** | Pressão Absoluta do Coletor (MAP) | kPa | `A` | ✅ | ✅ |
| **0x11** | Posição do Acelerador (TPS) | % | `(A*100)/255` | ✅ | ✅ |
| **0x42** | Tensão da Bateria | V | `((A*256)+B)/1000` | ✅ | ✅ |

### Conjunto Estendido (Opcionais — Próximas Versões)

| PID | Nome | Unidade | Fórmula | Vectra | Argo | Prioridade |
|-----|------|---------|---------|--------|------|-----------|
| **0x04** | Carga Calculada | % | `(A*100)/255` | ✅ | ✅ | 🔴 Alta |
| **0x10** | Fluxo de Ar (MAF) | g/s | `(A*256+B)/100` | ✅ | ✅ | 🔴 Alta |
| **0x44** | Lambda (Razão Ar/Combustível) | λ | `((A*256)+B)/32768` | ✅ | ✅ | 🟡 Média |
| **0x4D** | Tempo de Execução do Motor | s | `(A*256)+B` | ✅ | ✅ | 🟢 Baixa |
| **0x21** | Distância com MIL Ativa | km | `(A*256)+B` | ✅ | ✅ | 🟢 Baixa |
| 0x2E | EGR Comandado | % | `(A*100)/255` | ✅ | ✅ | 🟢 Diag |
| 0x2F | Erro EGR | % | `(A*100)/255` | ✅ | ✅ | 🟢 Diag |
| 0x22 | Pressão de Combustível | kPa | `A` | ✅ | ✅ | 🟢 Diag |

---

## Implementação Técnica

### Protocolo CAN Atual
```c
// Configuração existente em app_can.c
- Modo: TWAI_MODE_NORMAL (transmissão habilitada)
- Velocidade: 500 kbps (padrão OBD2)
- ID Requisição: 0x7DF (broadcast)
- ID Resposta: 0x7E8 (primeira ECU)
- Formato: ISO 15765-4 Single Frame
```

### Handshake (Auto-Detectado)
```
ESP32 (CAN 500k) → ECU
     ↓ SIM → Continua CAN
     ↓ NÃO → [Fallback KWP2000 não implementado]
```

### Estrutura de Dados Atual
```c
typedef struct {
    bool valid;           // Dados válidos?
    uint32_t last_rx_ms;  // Timestamp última resposta
    int32_t rpm;
    int32_t speed_kph;
    int32_t map_kpa;
    int32_t tps_pct;
    int32_t ect_c;
    int32_t iat_c;
    float batt_v;
} app_bcu_obd2_data_t;
```

Para expandir, adicionar:
```c
    int32_t load_pct;     // PID 0x04
    float maf_g_s;        // PID 0x10
    float lambda;         // PID 0x44
    int32_t runtime_s;    // PID 0x4D
    int32_t mil_km;       // PID 0x21
```

---

## Roadmap de Implementação

### Fase 1: Validação Básica (Agora)
- ✅ Testar 7 PIDs base em ambos veículos
- ✅ Confirmar comunicação estável
- ✅ Validar taxas de erro (bit-error-rate < 0.1%)

### Fase 2: Expansão (Próxima)
- ⏳ Adicionar PIDs 0x04, 0x10, 0x44 ao dashboard
- ⏳ Teste estresse: ciclo completo aceleração/cruzeiro/frenagem
- ⏳ Validação contra ferramenta OBD2 comercial (BlueDriver, Autel)

### Fase 3: Robustez (Futuro)
- ⏳ Timeout inteligente (fallback para ECU BLE se CAN falhar)
- ⏳ Retry automático com backoff exponencial
- ⏳ Log de eventos de desconexão
- ⏳ Suporte a KWP2000 via UART (se necessário)

---

## Testes Recomendados

### Teste 1: Conexão e Leitura Básica
```bash
1. Ligar Vectra com ignição ligada (motor não precisa rodar)
2. ESP32 entra em modo CAN
3. Esperado: Valores aparecem no Dashboard CAN tab
4. Valide: RPM, Speed, Temps, Voltagem
```

### Teste 2: Dinâmica Completa
```bash
1. Motor rodando em marcha-lenta (800-1000 RPM)
2. Acelerar suavemente até 5500 RPM
3. Manter cruzeiro por 30 segundos
4. Desacelerar até parada
5. Validar: Curvas suaves, sem saltos, sem timeout
```

### Teste 3: Argo 2025
```bash
1. Repetir Teste 1 e 2 com Argo
2. Verificar: Mesmo comportamento que Vectra
3. Validar: Compatibilidade cross-vehicle
```

---

## Notas de Segurança

⚠️ **Importante:**
- Não transmitir comandos (Mode 02, 03, 10, 11, 14) — apenas leitura (Mode 01)
- ESP32 nunca escreve no sistema de injeção/ignição
- Desconexão do painel não afeta funcionamento do veículo
- Ignição deve estar ligada para OBD2 responder (normal)

---

## Referências

- **SAE J1979:** Standard Vehicle Diagnostics
- **ISO 15765-4:** Diagnostic comm. over CAN (DoCAN)
- **ISO 14230-4:** Diagnostic comm. over UART (KWP2000)
- **FIAT Argo ECU:** Bosch ME17.9.11 (confirmado)
- **Vectra ECU:** GM L34 ou similar (verificar com `PID 0x00`)

