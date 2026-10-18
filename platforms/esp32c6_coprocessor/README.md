# ESP32-C6 co-processor firmware

The ESP32-C6 on the board (ESP32-C6-MINI-1U-H8) is the ESP32-P4's WiFi/BT
co-processor, connected via SDIO and driven by ESP-Hosted. It has no USB
port: its UART0 and BOOT (IO9) only reach the 4-pin pad field P1. The
ESP32-P4 can only reset it (EN on P4 GPIO54) and talk to it over SDIO.

So the C6 firmware is updated **by the P4**: the P4 firmware build carries a
C6 image in its `c6fw` partition and pushes it over SDIO with the
ESP-Hosted slave-OTA RPCs when it is newer than what the C6 runs
(`platforms/esp32p4/main/slave_ota.c`). UART is only the emergency path.

## What is built here

The ESP-Hosted **slave project of exactly the esp_hosted version the host
uses** (`platforms/esp32p4/main/idf_component.yml` pins it; currently
2.12.3), with stock configuration, plus the smartboard additions applied by
`apply_additions.py`:

- **Self-check** (`additions/c6_self_check.c`): an image that has never been
  confirmed in its slot is "under test"; it is confirmed by the first RPC
  the host completes. Without confirmation within 90 s of a boot, or after
  3 unconfirmed boots, it switches back to the other slot and restarts.
  It recognises "never confirmed" by itself (slot + ELF hash in NVS), so
  even the very first update away from a firmware without this mechanism
  is protected — as long as the new image boots far enough to start the
  check. Works with the existing bootloader (the bootloader cannot be
  replaced without UART).
- **Versioning**: image version `<esp-hosted version>-sb.<FW_REVISION>`;
  the revision is also reported to the host in the fw-version RPC's `build`
  field (stock firmware reports -1). Bump `FW_REVISION` for every change to
  the additions.

The C6 partition layout comes from the slave project
(`partitions.esp32c6.csv`: `ota_0`/`ota_1` 1920 KB each + `otadata`) and is
never changed by an update.

## Build

```sh
source ~/esp/v5.5.1/esp-idf/export.sh
platforms/esp32c6_coprocessor/build.sh
```

Output: `out/network_adapter.bin` (the plain image) and `out/c6fw.bin` (the
same image behind a 16-byte container header — `SBC6`, length, CRC32 — which
keeps esptool from rejecting a C6 image in a P4 flash command and lets the
P4 verify the whole image before transferring). The ESP32-P4 build flashes
`c6fw.bin` into the P4's `c6fw` partition (`idf.py flash`) if it exists.

## Rules

- Never flash the C6 directly without a recovery path; see
  `CPP_MIGRATION_PLAN.md` §0.
- Host and co-processor ESP-Hosted versions move together: bump the pin in
  `platforms/esp32p4/main/idf_component.yml` and `EXPECTED_VERSION` in
  `build.sh` in the same change, and keep the P4 working with the previous
  C6 version (the P4 updates first and talks to the old C6 before updating
  it).
