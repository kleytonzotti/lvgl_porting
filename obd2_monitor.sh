#!/bin/bash
# Monitor OBD2 logs apenas — filtra da saída serial do ESP32
# Uso: ./obd2_monitor.sh [PORTA]
# Ex:   ./obd2_monitor.sh /dev/ttyACM0

set -e

PORT="${1:-/dev/ttyACM0}"
BAUD=115200

if [ ! -e "$PORT" ]; then
    echo "❌ Porta não encontrada: $PORT"
    exit 1
fi

# Cores ANSI
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
GRAY='\033[0;37m'
RESET='\033[0m'

echo -e "${BLUE}═══════════════════════════════════════════════════${RESET}"
echo -e "${BLUE}OBD2 Monitor — Filtrando logs de $PORT${RESET}"
echo -e "${BLUE}═══════════════════════════════════════════════════${RESET}"
echo ""
echo -e "${GRAY}[OBD2-TX]    = Requisição de PID transmitida${RESET}"
echo -e "${GREEN}[OBD2-RX]    = Resposta recebida (valores decodificados)${RESET}"
echo -e "${RED}[OBD2-STALE] = Dados não atualizados por 2s${RESET}"
echo -e "${YELLOW}[OBD2-GET]   = Leitura de dados (dashboard/decoder)${RESET}"
echo -e "${BLUE}[OBD2]       = Ativação/desativação${RESET}"
echo ""
echo -e "${GRAY}Pressione Ctrl+C para sair${RESET}"
echo ""

# Pipe do monitor com filtro de cores
source /home/zotti/.espressif/v5.5.4/esp-idf/export.sh > /dev/null 2>&1

idf.py -p "$PORT" monitor 2>/dev/null | while IFS= read -r line; do
    # Pula linhas sem [OBD2-]
    if [[ ! "$line" =~ \[OBD2- ]]; then
        continue
    fi

    # Extrai timestamp se houver (formato: I (123456) TAG)
    timestamp=$(echo "$line" | grep -oP 'I \(\d+\)' || echo "")

    # Coloriza conforme tipo
    if [[ "$line" =~ \[OBD2-TX\] ]]; then
        # Azul para transmissão
        filtered="${line//\[OBD2-TX\]/${BLUE}[OBD2-TX]${RESET}}"
        echo -e "$filtered"
    elif [[ "$line" =~ \[OBD2-RX\] ]]; then
        # Verde para recepção (sucesso)
        filtered="${line//\[OBD2-RX\]/${GREEN}[OBD2-RX]${RESET}}"
        echo -e "$filtered"
    elif [[ "$line" =~ \[OBD2-STALE\] ]]; then
        # Vermelho para stale (timeout)
        filtered="${line//\[OBD2-STALE\]/${RED}[OBD2-STALE]${RESET}}"
        echo -e "$filtered"
    elif [[ "$line" =~ \[OBD2-GET\] ]]; then
        # Amarelo para GET (leitura)
        filtered="${line//\[OBD2-GET\]/${YELLOW}[OBD2-GET]${RESET}}"
        echo -e "$filtered"
    elif [[ "$line" =~ \[OBD2\] ]]; then
        # Azul para eventos gerais
        filtered="${line//\[OBD2\]/${BLUE}[OBD2]${RESET}}"
        echo -e "$filtered"
    fi
done
