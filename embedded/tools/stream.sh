#!/bin/bash
# stream.sh - Restart ETM Fabric streaming to Rerun
#
# Usage: ./tools/stream.sh [port]
#   port: Serial port (default: /dev/ttyACM0)

PORT="${1:-/dev/ttyACM0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== ETM Fabric Stream ==="
echo "Port: $PORT"

# Kill existing monitor
pkill -f "rerun_etm_monitor" 2>/dev/null && echo "Killed existing monitor" || true
sleep 1

# Reset ESP32
echo "Resetting ESP32..."
python3 -c "
import serial, time
ser = serial.Serial('$PORT', 115200)
ser.setDTR(False); time.sleep(0.1); ser.setDTR(True); time.sleep(0.1); ser.setDTR(False)
ser.close()
"

# Wait for boot
echo "Waiting for ESP32 to boot..."
sleep 8

# Start monitor
echo "Starting Rerun monitor..."
cd "$REPO_DIR" && python tools/rerun_etm_monitor.py "$PORT" &
sleep 3

if pgrep -f "rerun_etm_monitor" > /dev/null; then
    echo ""
    echo "Stream running! Check Rerun viewer."
    echo "Press Ctrl+C to stop."
    echo ""
    # Wait for user interrupt
    wait
else
    echo "ERROR: Monitor failed to start"
    exit 1
fi
