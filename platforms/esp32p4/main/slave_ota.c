/*
 * ESP32-C6 co-processor firmware update over SDIO — see slave_ota.h.
 *
 * Replaces the earlier LittleFS-based variant, which started the transfer
 * before the SDIO transport was up and could not reset the C6 (wrong reset
 * GPIO at the time) — the cause of the April 2026 incident that needed the
 * UART pads to recover.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slave_ota.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "config.h"
#include "fonts.h"
#include "lvgl.h"

static const char *TAG = "slave_ota";

/* Last co-processor firmware version seen (for display), "" if unknown. */
static char s_cp_version[40] = "";

const char *slave_ota_coprocessor_version(void) { return s_cp_version; }

/* Config key c6_updates ("0" = never update the C6), read once at boot. */
static bool s_updates_enabled = true;
static volatile bool s_busy = false;

bool slave_ota_busy(void) { return s_busy; }

#define C6FW_SUBTYPE 0x40
#define OTA_CHUNK_SIZE 1500
#define MAX_ATTEMPTS_PER_VERSION 2

/* 1 = run the whole check and log the decision, but never transfer anything
 * to the C6 (for verifying image detection/version logic on new hardware). */
#ifndef C6_OTA_DRY_RUN
#define C6_OTA_DRY_RUN 0
#endif

typedef struct {
    int major, minor, patch, rev;
} fw_version_t;

/* ── Image in the c6fw partition ──────────────────────────────────────────── */

/* The c6fw partition holds a 16-byte container header followed by the C6
 * app image (written by platforms/esp32c6_coprocessor/build.sh):
 *   "SBC6" | u32 LE image length | u32 LE CRC32 of the image | 4 bytes 0 */
#define C6FW_HEADER_SIZE 16

/* Written by the web flasher in place of the C6 image when the user chose
 * not to update the co-processor (tools/web-flasher). */
#define C6FW_SKIP_MAGIC "SBNO"
#define C6FW_MAGIC "SBC6"

typedef struct {
    const esp_partition_t *part;
    size_t base;    /* offset of the image inside the partition */
    size_t length;  /* image length */
    uint32_t crc;   /* CRC32 of the image (from the verified container) */
    char version[33];
    fw_version_t ver;
} c6_image_t;

/* "2.12.3-sb.1" -> {2,12,3,1}; without "-sb.N" the revision is 0. */
static bool parse_version(const char *s, fw_version_t *out)
{
    memset(out, 0, sizeof(*out));
    if (sscanf(s, "%d.%d.%d", &out->major, &out->minor, &out->patch) != 3) return false;
    const char *sb = strstr(s, "-sb.");
    if (sb) out->rev = atoi(sb + 4);
    return true;
}

static int compare_versions(const fw_version_t *a, const fw_version_t *b)
{
    if (a->major != b->major) return a->major < b->major ? -1 : 1;
    if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
    if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
    if (a->rev != b->rev) return a->rev < b->rev ? -1 : 1;
    return 0;
}

/* Validates the image header (magic, chip) and computes the image length
 * from the segment table: header + segments, padded so the checksum byte
 * ends on a 16-byte boundary, + 32 bytes SHA-256 if appended. */
const char *slave_ota_c6fw_label_for(const esp_partition_t *app_partition)
{
    if (!app_partition) return NULL;
    if (app_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return "c6fw_0";
    if (app_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return "c6fw_1";
    return NULL;
}

static bool load_image_info(c6_image_t *img)
{
    /* The C6 image released together with the running app. */
    const char *C6FW_LABEL = slave_ota_c6fw_label_for(esp_ota_get_running_partition());
    if (!C6FW_LABEL) return false;
    img->part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, C6FW_SUBTYPE, C6FW_LABEL);
    if (!img->part) {
        ESP_LOGI(TAG, "no %s partition", C6FW_LABEL);
        return false;
    }

    uint8_t container[C6FW_HEADER_SIZE];
    if (esp_partition_read(img->part, 0, container, sizeof(container)) != ESP_OK) return false;
    if (memcmp(container, C6FW_MAGIC, 4) != 0) {
        ESP_LOGI(TAG, "%s partition holds no co-processor firmware", C6FW_LABEL);
        return false;
    }
    uint32_t declared_len, declared_crc;
    memcpy(&declared_len, container + 4, 4);
    memcpy(&declared_crc, container + 8, 4);
    if (declared_len == 0 || declared_len > img->part->size - C6FW_HEADER_SIZE) {
        ESP_LOGE(TAG, "%s container declares an invalid length %" PRIu32, C6FW_LABEL, declared_len);
        return false;
    }
    img->base = C6FW_HEADER_SIZE;

    esp_image_header_t hdr;
    if (esp_partition_read(img->part, img->base, &hdr, sizeof(hdr)) != ESP_OK) return false;
    if (hdr.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "%s container holds no firmware image", C6FW_LABEL);
        return false;
    }
    if (hdr.chip_id != ESP_CHIP_ID_ESP32C6) {
        ESP_LOGE(TAG, "%s image is for chip id 0x%04x, not ESP32-C6 — ignoring", C6FW_LABEL, hdr.chip_id);
        return false;
    }

    size_t offset = sizeof(hdr);
    for (int i = 0; i < hdr.segment_count; i++) {
        esp_image_segment_header_t seg;
        if (esp_partition_read(img->part, img->base + offset, &seg, sizeof(seg)) != ESP_OK) return false;
        if (i == 0) {
            esp_app_desc_t desc; /* app description = start of the first segment */
            if (esp_partition_read(img->part, img->base + offset + sizeof(seg), &desc, sizeof(desc)) != ESP_OK) return false;
            if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
                ESP_LOGE(TAG, "%s image has no app description", C6FW_LABEL);
                return false;
            }
            snprintf(img->version, sizeof(img->version), "%s", desc.version);
        }
        offset += sizeof(seg) + seg.data_len;
        if (offset > img->part->size) {
            ESP_LOGE(TAG, "%s image segment table is corrupt", C6FW_LABEL);
            return false;
        }
    }
    offset = (offset | 15) + 1; /* checksum byte at the end of a 16-byte block */
    if (hdr.hash_appended) offset += 32;
    if (offset != declared_len) {
        ESP_LOGE(TAG, "%s image length %u does not match its container (%" PRIu32 ")", C6FW_LABEL,
                 (unsigned)offset, declared_len);
        return false;
    }
    img->length = offset;

    /* CRC over the whole image, so a partially written or corrupted
     * partition is never transferred. */
    uint8_t *buf = malloc(4096);
    if (!buf) return false;
    uint32_t crc = 0;
    for (size_t off = 0; off < img->length; off += 4096) {
        size_t n = img->length - off < 4096 ? img->length - off : 4096;
        if (esp_partition_read(img->part, img->base + off, buf, n) != ESP_OK) {
            free(buf);
            return false;
        }
        crc = esp_rom_crc32_le(crc, buf, n);
    }
    free(buf);
    if (crc != declared_crc) {
        ESP_LOGE(TAG, "%s image CRC mismatch (0x%08" PRIx32 " != 0x%08" PRIx32 ")", C6FW_LABEL, crc, declared_crc);
        return false;
    }
    img->crc = crc;

    if (!parse_version(img->version, &img->ver)) {
        ESP_LOGE(TAG, "%s image has an unparseable version '%s'", C6FW_LABEL, img->version);
        return false;
    }
    return true;
}

/* ── Attempt bookkeeping (never loop on an image that keeps failing) ──────── *
 * Counted per image (version + CRC), and never reset by seeing the new
 * version running: right after an update the C6 still runs it "under test"
 * and may fall back minutes later. A different image (new version or new
 * build) starts again at zero. */

static int attempts_for(const c6_image_t *img, bool increment)
{
    char key[48];
    snprintf(key, sizeof(key), "%s:%08" PRIx32, img->version, img->crc);

    nvs_handle_t h;
    if (nvs_open("c6upd", NVS_READWRITE, &h) != ESP_OK) return 0;
    char stored[48] = {0};
    size_t len = sizeof(stored);
    uint8_t tries = 0;
    if (nvs_get_str(h, "img", stored, &len) == ESP_OK && strcmp(stored, key) == 0) {
        nvs_get_u8(h, "tries", &tries);
    }
    if (increment) {
        tries++;
        nvs_set_str(h, "img", key);
        nvs_set_u8(h, "tries", tries);
        nvs_commit(h);
    }
    nvs_close(h);
    return tries;
}

/* ── Progress overlay (design guide: true black, sand accent) ─────────────── */

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_status = NULL;

static void overlay_show(void)
{
    if (!bsp_display_lock(0)) return;
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_overlay, 64, 0);
    lv_obj_set_style_pad_row(s_overlay, 20, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE); /* block input meanwhile */

    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "Updating WiFi module");
    lv_obj_set_style_text_font(title, font_manrope_600_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xECE9E3), 0);

    s_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_bar, LV_PCT(100), 16);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_radius(s_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x1F1E1C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xD6B98A), LV_PART_INDICATOR);

    s_status = lv_label_create(s_overlay);
    lv_label_set_text(s_status, "Please keep the device powered.");
    lv_obj_set_style_text_font(s_status, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xA39F97), 0);
    bsp_display_unlock();
}

static void overlay_update(int percent, const char *status)
{
    if (!s_overlay || !bsp_display_lock(0)) return;
    if (percent >= 0) lv_bar_set_value(s_bar, percent, LV_ANIM_OFF);
    if (status) lv_label_set_text(s_status, status);
    bsp_display_unlock();
}

static void overlay_hide(void)
{
    if (!s_overlay || !bsp_display_lock(0)) return;
    lv_obj_delete(s_overlay);
    s_overlay = s_bar = s_status = NULL;
    bsp_display_unlock();
}

/* ── Transfer ─────────────────────────────────────────────────────────────── */

static esp_err_t transfer(const c6_image_t *img)
{
    uint8_t *chunk = malloc(OTA_CHUNK_SIZE);
    if (!chunk) return ESP_ERR_NO_MEM;

    esp_err_t ret = esp_hosted_slave_ota_begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s", esp_err_to_name(ret));
        goto out;
    }

    int last_percent = -1;
    for (size_t off = 0; off < img->length; off += OTA_CHUNK_SIZE) {
        size_t n = img->length - off < OTA_CHUNK_SIZE ? img->length - off : OTA_CHUNK_SIZE;
        ret = esp_partition_read(img->part, img->base + off, chunk, n);
        if (ret == ESP_OK) ret = esp_hosted_slave_ota_write(chunk, n);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "transfer failed at %u/%u: %s", (unsigned)off, (unsigned)img->length,
                     esp_err_to_name(ret));
            goto out;
        }
        int percent = (int)((off + n) * 100 / img->length);
        if (percent != last_percent) {
            last_percent = percent;
            overlay_update(percent, NULL);
            if (percent % 10 == 0) ESP_LOGI(TAG, "transfer %d%%", percent);
        }
    }

    /* The C6 verifies the complete image (checksum/SHA) here and refuses an
     * incomplete or corrupt one; its running firmware stays untouched. */
    ret = esp_hosted_slave_ota_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed (image rejected): %s", esp_err_to_name(ret));
        goto out;
    }
    ret = esp_hosted_slave_ota_activate();
    if (ret != ESP_OK) ESP_LOGE(TAG, "ota_activate failed: %s", esp_err_to_name(ret));

out:
    free(chunk);
    return ret;
}

void slave_ota_load_settings(void)
{
    const char *label = slave_ota_c6fw_label_for(esp_ota_get_running_partition());
    const esp_partition_t *part =
        label ? esp_partition_find_first(ESP_PARTITION_TYPE_DATA, C6FW_SUBTYPE, label) : NULL;
    char magic[4];
    if (part && esp_partition_read(part, 0, magic, sizeof(magic)) == ESP_OK &&
        memcmp(magic, C6FW_SKIP_MAGIC, 4) == 0) {
        /* Erased, so turning the setting back on later sticks. */
        ESP_LOGW(TAG, "installed without co-processor updates: turning them off");
        updateConfig("c6_updates", "0");
        saveConfig();
        esp_partition_erase_range(part, 0, part->erase_size);
    }
    const char *v = getConfig("c6_updates");
    s_updates_enabled = !(v && strcmp(v, "0") == 0);
}

void slave_ota_update_if_needed(void)
{
    if (!s_updates_enabled) {
        ESP_LOGI(TAG, "co-processor updates are turned off");
        return;
    }
    c6_image_t img = {0};
    if (!load_image_info(&img)) return;

    if (esp_hosted_connect_to_slave() != ESP_OK) {
        ESP_LOGW(TAG, "co-processor not reachable — skipping update check");
        return;
    }

    esp_hosted_coprocessor_fwver_t running_raw = {0};
    if (esp_hosted_get_coprocessor_fwversion(&running_raw) != ESP_OK) {
        /* E.g. factory firmware that does not implement the version RPC.
         * Not updated automatically until that path has been tested. */
        ESP_LOGW(TAG, "co-processor does not report its firmware version — not updating");
        return;
    }
    fw_version_t running = {(int)running_raw.major1, (int)running_raw.minor1, (int)running_raw.patch1,
                            running_raw.build > 0 ? (int)running_raw.build : 0};

    if (compare_versions(&img.ver, &running) <= 0) {
        ESP_LOGI(TAG, "co-processor firmware %d.%d.%d-sb.%d is up to date (bundled %s)", running.major,
                 running.minor, running.patch, running.rev, img.version);
        return;
    }

    if (C6_OTA_DRY_RUN) {
        ESP_LOGW(TAG, "DRY RUN: would update co-processor %d.%d.%d-sb.%d -> %s (%u bytes)", running.major,
                 running.minor, running.patch, running.rev, img.version, (unsigned)img.length);
        return;
    }

    int attempts = attempts_for(&img, false);
    if (attempts >= MAX_ATTEMPTS_PER_VERSION) {
        ESP_LOGE(TAG, "update to %s already failed %d times — not trying again", img.version, attempts);
        return;
    }
    attempts_for(&img, true);

    ESP_LOGW(TAG, "updating co-processor %d.%d.%d-sb.%d -> %s (%u bytes, attempt %d/%d)", running.major,
             running.minor, running.patch, running.rev, img.version, (unsigned)img.length, attempts + 1,
             MAX_ATTEMPTS_PER_VERSION);

    s_busy = true;
    overlay_show();
    esp_err_t ret = transfer(&img);
    if (ret != ESP_OK) {
        overlay_update(-1, "Update failed — keeping the current version.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        overlay_hide();
        s_busy = false;
        return;
    }

    /* Restarting the P4 also resets the C6 (EN on GPIO54) into the new
     * image; it confirms itself once this host talks to it again. */
    overlay_update(100, "Done. Restarting...");
    ESP_LOGW(TAG, "co-processor update transferred — restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* ── Diagnostics ──────────────────────────────────────────────────────────── */

void slave_ota_log_coprocessor_info(void)
{
    uint32_t chip_id = 0;
    char target[16] = {0};
    if (esp_hosted_get_cp_info(&chip_id, target, sizeof(target)) == ESP_OK && target[0]) {
        ESP_LOGI(TAG, "Co-processor: %s (chip id %lu)", target, (unsigned long)chip_id);
    }

    esp_hosted_coprocessor_fwver_t ver = {0};
    if (esp_hosted_get_coprocessor_fwversion(&ver) == ESP_OK) {
        snprintf(s_cp_version, sizeof(s_cp_version), "%lu.%lu.%lu-sb.%ld", (unsigned long)ver.major1,
                 (unsigned long)ver.minor1, (unsigned long)ver.patch1, (long)(ver.build > 0 ? ver.build : 0));
        ESP_LOGI(TAG, "Co-processor firmware: %s", s_cp_version);
    } else {
        /* Factory images (e.g. the ESP-Brookesia-based one some boards ship
         * with) may not implement the version RPC at all. */
        ESP_LOGW(TAG, "Co-processor firmware version not available");
    }
}
