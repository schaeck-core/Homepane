#!/usr/bin/env bash
# Builds the ESP32-C6 co-processor firmware: the ESP-Hosted slave project of
# exactly the esp_hosted version the ESP32-P4 host uses, plus the smartboard
# additions (self-check, versioning). See README.md.
#
# Output: out/network_adapter.bin (picked up by the ESP32-P4 build, which
# flashes it into the P4's c6fw partition).
#
# Requires an exported ESP-IDF v5.5.x environment (idf.py on PATH).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
P4="$HERE/../esp32p4"
HOSTED="$P4/managed_components/espressif__esp_hosted"
EXPECTED_VERSION="2.12.3"
REVISION="$(tr -d '[:space:]' < "$HERE/FW_REVISION")"
# Test knobs (hardware test of the self-check fallback; never ship these):
#   C6_TEST_REVISION=<n>       build with this revision instead of FW_REVISION
#   C6_TEST_NEVER_CONFIRM=1    image never confirms itself -> must fall back
EXTRA_ARGS=()
if [ -n "${C6_TEST_REVISION:-}" ]; then REVISION="$C6_TEST_REVISION"; fi
if [ "${C6_TEST_NEVER_CONFIRM:-0}" = "1" ]; then
    EXTRA_ARGS+=(--test-never-confirm)
    echo "!!! Building a TEST image that never confirms itself — do not ship !!!"
fi

if [ ! -d "$HOSTED/slave" ]; then
    echo "esp_hosted not downloaded yet — resolving the ESP32-P4 project's dependencies"
    (cd "$P4" && idf.py reconfigure >/dev/null)
fi
VERSION="$(sed -n 's/^version: *//p' "$HOSTED/idf_component.yml" | tr -d "\"' ")"
if [ "$VERSION" != "$EXPECTED_VERSION" ]; then
    echo "esp_hosted is $VERSION, expected $EXPECTED_VERSION — host and co-processor must match." >&2
    echo "Update EXPECTED_VERSION here and the pin in platforms/esp32p4/main/idf_component.yml together." >&2
    exit 1
fi

WORK="$HERE/build/project"
rm -rf "$WORK"
mkdir -p "$WORK"
cp -R "$HOSTED/slave/." "$WORK/"
rm -rf "$WORK/build" "$WORK/sdkconfig" "$WORK/managed_components" "$WORK/dependencies.lock"
python3 "$HERE/apply_additions.py" "$WORK" "$REVISION" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"

cd "$WORK"
idf.py set-target esp32c6
idf.py build

mkdir -p "$HERE/out"
cp build/network_adapter.bin "$HERE/out/network_adapter.bin"
# c6fw.bin = 16-byte container header + image, written into the P4's c6fw
# partition. The header keeps esptool from treating the payload as an
# (ESP32-C6, i.e. wrong-chip) app image when flashing the P4, and gives the
# P4 a length + CRC32 to verify before it transfers anything:
#   "SBC6" | u32 LE image length | u32 LE CRC32 of the image | 4 bytes zero
python3 - "$HERE/out/network_adapter.bin" "$HERE/out/c6fw.bin" <<'PY'
import struct, sys, zlib
image = open(sys.argv[1], "rb").read()
header = b"SBC6" + struct.pack("<II", len(image), zlib.crc32(image) & 0xFFFFFFFF) + bytes(4)
open(sys.argv[2], "wb").write(header + image)
PY
echo "C6 firmware ${EXPECTED_VERSION}-sb.${REVISION} -> $HERE/out/c6fw.bin ($(wc -c < "$HERE/out/network_adapter.bin") bytes + 16 header)"
