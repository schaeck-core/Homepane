#!/usr/bin/env python3
"""
make_manifests.py — firmware parts and ESP Web Tools manifests for the web
flasher, from an ESP-IDF build of platforms/esp32p4.

Writes into OUT:
  firmware/*.bin          the parts listed in build/flash_args, plus c6-skip.bin
  manifest.json           install with the ESP32-C6 co-processor firmware
  manifest-no-c6.json     install without it: a marker (c6-skip.bin) takes the
                          C6 image's place; the panel then turns co-processor
                          updates off (platforms/esp32p4/main/slave_ota.c)

Offsets come from build/flash_args and the built partition table, so they
always match platforms/esp32p4/partitions.csv. Used by the release workflow
and by serve_local.py.

Usage:
    python3 tools/web-flasher/make_manifests.py --build-dir platforms/esp32p4/build --out site --version 0.2.0
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
TEMPLATE = SCRIPT_DIR / "manifest.template.json"

# Release names for build outputs
NAMES = {"lvgl_homeassistant.bin": "homepane.bin"}

# Must match C6FW_SKIP_MAGIC in platforms/esp32p4/main/slave_ota.c
SKIP_MARKER = b"SBNO" + bytes(12)
SKIP_NAME = "c6-skip.bin"
C6_NAME = "c6fw.bin"


def partition_offset(table: Path, label: str) -> int | None:
    """Offset of a partition in an ESP-IDF partition table binary."""
    data = table.read_bytes()
    for i in range(0, len(data) - 31, 32):
        magic, _type, _subtype, offset, _size = struct.unpack_from("<HBBII", data, i)
        if magic != 0x50AA:
            break
        name = data[i + 12:i + 28].split(b"\0", 1)[0].decode()
        if name == label:
            return offset
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", default=None)
    args = parser.parse_args()

    build = args.build_dir
    firmware = args.out / "firmware"
    firmware.mkdir(parents=True, exist_ok=True)

    parts = []
    for line in (build / "flash_args").read_text().splitlines():
        if not line.startswith("0x"):
            continue
        offset, rel = line.split(maxsplit=1)
        src = (build / rel).resolve()
        name = NAMES.get(src.name, src.name)
        shutil.copy(src, firmware / name)
        parts.append({"path": f"firmware/{name}", "offset": int(offset, 16)})
        print(f"  {name:24s} 0x{int(offset, 16):07x}")
    parts.sort(key=lambda p: p["offset"])

    # The C6 image belongs to the first app slot (ota_0 boots after flashing).
    c6_offset = partition_offset(build / "partition_table" / "partition-table.bin", "c6fw_0")
    if c6_offset is None:
        print("ERROR: no c6fw_0 partition in the partition table", file=sys.stderr)
        return 1
    (firmware / SKIP_NAME).write_bytes(SKIP_MARKER)
    without_c6 = [p for p in parts if p["offset"] != c6_offset]
    without_c6.append({"path": f"firmware/{SKIP_NAME}", "offset": c6_offset})
    without_c6.sort(key=lambda p: p["offset"])
    if not any(p["path"] == f"firmware/{C6_NAME}" for p in parts):
        print("WARNING: no C6 image in this build (platforms/esp32c6_coprocessor/build.sh); "
              "manifest.json installs without it")

    template = json.loads(TEMPLATE.read_text())
    if args.version:
        template["version"] = args.version
    for name, build_parts in (("manifest.json", parts), ("manifest-no-c6.json", without_c6)):
        manifest = json.loads(json.dumps(template))
        manifest["builds"][0]["parts"] = build_parts
        (args.out / name).write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"  wrote {args.out / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
