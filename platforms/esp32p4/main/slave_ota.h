/*
 * ESP32-C6 co-processor firmware update over SDIO.
 *
 * The C6 has no USB port of its own; its firmware is updated by the P4:
 * the image built by platforms/esp32c6_coprocessor/build.sh is flashed into
 * the P4's c6fw_0/c6fw_1 partition (one per app slot) together with the app, and the P4 pushes it to
 * the C6 with the ESP-Hosted slave-OTA RPCs when it is newer than what the
 * C6 runs. The C6 firmware protects itself against a bad update (falls back
 * to its previous slot if the host cannot talk to the new image), see
 * platforms/esp32c6_coprocessor/README.md.
 */
#ifndef SLAVE_OTA_H
#define SLAVE_OTA_H

#include <stdbool.h>

#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read the co-processor update setting (config key c6_updates, "0" = off).
 * Call once after loadConfig(), before the UI runs. A web flasher install
 * without the C6 image leaves a marker in the c6fw partition instead; it is
 * turned into c6_updates=0 here.
 */
void slave_ota_load_settings(void);

/** True while an image is being transferred to the C6 (ends in a restart on success). */
bool slave_ota_busy(void);

/**
 * Update the C6 from the c6fw partition if that image is newer than the
 * running C6 firmware. Call after the P4 itself has been confirmed
 * (esp_ota_mark_app_valid_cancel_rollback()), with the display running:
 * shows a progress overlay while transferring. Brings up the ESP-Hosted
 * transport if needed.
 *
 * Returns without doing anything if co-processor updates are turned off,
 * there is no valid C6 image, it is not newer, the running C6 firmware does
 * not report its version, or two attempts for this image version already
 * failed. On a successful transfer
 * it restarts the P4 (which also resets the C6 into the new image) and does
 * not return.
 */
void slave_ota_update_if_needed(void);

/**
 * Log the co-processor's chip and firmware version (diagnostics only).
 * Call once the ESP-Hosted transport is up (after esp_hosted_connect_to_slave()).
 */
void slave_ota_log_coprocessor_info(void);

/**
 * Label of the c6fw partition belonging to an app slot ("c6fw_0" for ota_0,
 * "c6fw_1" for ota_1), NULL for anything else. The P4 OTA updater writes a
 * new C6 image into the partition of the slot it installs the app into.
 */
const char *slave_ota_c6fw_label_for(const esp_partition_t *app_partition);

/** Co-processor firmware version last read from the C6 ("" if unknown). */
const char *slave_ota_coprocessor_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SLAVE_OTA_H */
