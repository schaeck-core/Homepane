#ifndef C6_SELF_CHECK_H
#define C6_SELF_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Self-check for co-processor firmware updates pushed by the ESP32-P4 host
 * (smartboard addition to the ESP-Hosted slave, see
 * platforms/esp32c6_coprocessor/README.md).
 *
 * Every image that has never been confirmed in its slot is "under test":
 * it is confirmed as soon as the host completes one RPC with it. If that
 * does not happen within C6_SELF_CHECK_CONFIRM_TIMEOUT_MS of a boot, or the
 * image has been booted more than C6_SELF_CHECK_MAX_TRIES times without
 * confirmation, the firmware switches back to the other slot and restarts.
 *
 * Because an image detects "never confirmed" on its own (slot + ELF hash
 * remembered in NVS), this also protects the first update away from a
 * firmware that knows nothing about this mechanism, and it works with any
 * bootloader (no CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE needed — the
 * bootloader cannot be replaced without UART access).
 */

#define C6_SELF_CHECK_MAX_TRIES 3
#define C6_SELF_CHECK_CONFIRM_TIMEOUT_MS (90 * 1000)

/* Call first thing in app_main(), after nvs_flash_init(). */
void c6_self_check_on_boot(void);

/* Call after the host successfully completed an RPC. Cheap after the first call. */
void c6_self_check_confirm(void);

#ifdef __cplusplus
}
#endif

#endif /* C6_SELF_CHECK_H */
