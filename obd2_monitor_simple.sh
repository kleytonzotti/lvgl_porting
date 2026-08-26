#!/bin/bash
# Monitor OBD2 simples — roda monitor e filtra paralela
# Uso: ./obd2_monitor_simple.sh [PORTA]

PORT="${1:-/dev/ttyACM0}"

if [ ! -e "$PORT" ]; then
    echo "❌ Porta não encontrada: $PORT"
    exit 1
fi

# Cores
TXN='\033[36m'  # Cyan: requisição
RXN='\033[32m'  # Verde: resposta
STA='\033[31m'  # Vermelho: stale
GET='\033[33m'  # Amarelo: get
ACT='\033[34m'  # Azul: ativação
RST='\033[0m'   # Reset

source /home/zotti/.espressif/v5.5.4/esp-idf/export.sh > /dev/null 2>&1

echo "🔍 OBD2 Monitor — $PORT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

idf.py -p "$PORT" monitor 2>&1 | grep -E --color=never "\[OBD2-(TX|RX|STALE|GET)\]|\[OBD2\] " | while read line; do
    if [[ "$line" =~ "[OBD2-TX]" ]]; then
        echo -e "${TXN}📤${RST} $line"
    elif [[ "$line" =~ "[OBD2-RX]" ]]; then
        echo -e "${RXN}📥${RST} $line"
    elif [[ "$line" =~ "[OBD2-STALE]" ]]; then
        echo -e "${STA}⏱️ ${RST} $line"
    elif [[ "$line" =~ "[OBD2-GET]" ]]; then
        echo -e "${GET}📊${RST} $line"
    elif [[ "$line" =~ "[OBD2]" ]]; then
        echo -e "${ACT}⚙️ ${RST} $line"
    else
        echo -e "$line"
    fi
done
