#!/usr/bin/env bash
# upload_slave_fw.sh — Upload ESP32-C6 co-proc firmware to LittleFS on ESP32-P4
#
# Usage:
#   ./scripts/upload_slave_fw.sh [PORT] [SLAVE_BIN]
#
# Defaults:
#   PORT       = auto-detect (first /dev/cu.usbmodem*)
#   SLAVE_BIN  = /tmp/esp_hosted_slave_c6/build/network_adapter.bin
#
# What it does:
#   1. Creates a temporary LittleFS image containing network_adapter.bin
#   2. Writes the image to the "storage" partition on the ESP32-P4
#   3. The P4 firmware will detect the file on next boot and OTA-flash
#      it to the ESP32-C6 co-processor via SDIO.
#
# Prerequisites:
#   - The ESP32-P4 must be connected via USB
#   - littlefs-python already installed in ESP-IDF venv (done automatically)

set -euo pipefail

# Prefer the ESP-IDF Python (Python 3.14, has required packages)
IDF_PYTHON="${HOME}/.espressif/python_env/idf5.5_py3.14_env/bin/python"
if [ ! -x "$IDF_PYTHON" ]; then
    # Fallback: any python3 in PATH
    IDF_PYTHON="$(command -v python3 || true)"
fi
if [ -z "$IDF_PYTHON" ]; then
    echo "ERROR: No Python 3 found. Source ESP-IDF first:"
    echo "  source ~/esp/v5.5.1/esp-idf/export.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Arguments ---
PORT="${1:-}"
SLAVE_BIN="${2:-/tmp/esp_hosted_slave_c6/build/network_adapter.bin}"

# Auto-detect port
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | grep -v Bluetooth | head -1 || true)
    if [ -z "$PORT" ]; then
        echo "ERROR: No USB serial port found. Connect the ESP32-P4 and try again."
        exit 1
    fi
    echo "Auto-detected port: $PORT"
fi

# Validate slave binary
if [ ! -f "$SLAVE_BIN" ]; then
    echo "ERROR: Slave firmware binary not found: $SLAVE_BIN"
    echo ""
    echo "Build it first:"
    echo "  source ~/esp/v5.5.1/esp-idf/export.sh"
    echo "  cd /tmp/esp_hosted_slave_c6"
    echo "  idf.py set-target esp32c6 && idf.py build"
    exit 1
fi

echo "=== ESP32-C6 Slave Firmware Upload ==="
echo "Port:      $PORT"
echo "Firmware:  $SLAVE_BIN"
echo "Size:     $(wc -c < "$SLAVE_BIN") bytes"
echo "Python:    $IDF_PYTHON"
echo ""

# Ensure littlefs-python is available in the chosen interpreter
"$IDF_PYTHON" -c "import littlefs" 2>/dev/null || {
    echo "Installing littlefs-python..."
    "$IDF_PYTHON" -m pip install --quiet littlefs-python
}

# --- Create temporary LittleFS image ---
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

LITTLEFS_IMG="$WORK_DIR/littlefs.img"
FW_COPY="$WORK_DIR/network_adapter.bin"
cp "$SLAVE_BIN" "$FW_COPY"

# storage partition = 8 MB (matches partitions.csv)
PARTITION_SIZE=$((8 * 1024 * 1024))

echo "Creating LittleFS image (${PARTITION_SIZE} bytes)..."

"$IDF_PYTHON" - "$FW_COPY" "$LITTLEFS_IMG" "$PARTITION_SIZE" <<'PYEOF'
import sys, littlefs

fw_path, img_path, part_size = sys.argv[1], sys.argv[2], int(sys.argv[3])

block_size  = 4096
block_count = part_size // block_size

lfs = littlefs.LittleFS(block_size=block_size, block_count=block_count, name_max=255)

with open(fw_path, 'rb') as f:
    data = f.read()

with lfs.open('network_adapter.bin', 'wb') as lf:
    lf.write(data)

lfs.unmount()

img = bytes(lfs.context.buffer)
with open(img_path, 'wb') as f:
    f.write(img)

print(f'LittleFS image: {len(img)} bytes, firmware: {len(data)} bytes')
PYEOF

echo "Writing LittleFS image to 'storage' partition (offset 0x410000)..."

# Use esptool from the ESP-IDF Python env
"$IDF_PYTHON" -m esptool --chip esp32p4 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash 0x410000 "$LITTLEFS_IMG"

echo ""
echo "=== Upload complete ==="
echo "The ESP32-P4 will detect the firmware on next boot and"
echo "OTA-flash it to the ESP32-C6 co-processor via SDIO."
echo ""
echo "Reset the board or flash+monitor to trigger the update:"
echo "  source ~/esp/v5.5.1/esp-idf/export.sh"
echo "  cd platforms/esp32p4"
echo "  idf.py -p $PORT flash monitor"
