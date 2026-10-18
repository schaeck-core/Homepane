#include "c6_self_check.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "c6_self_check";
static const char *NVS_NS = "c6_selfchk";

/* NVS keys:
 *   "ok"    — id of the image last confirmed by the host
 *   "test"  — id of the image currently under test
 *   "tries" — boots of the image under test so far
 * id = "<slot label>:<first 16 hex chars of the ELF SHA-256>" */

static volatile bool s_under_test = false;
static volatile bool s_confirmed = false;
static esp_timer_handle_t s_timer = NULL;
static char s_id[40];

static void make_id(char *out, size_t out_len)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    char sha[17] = {0};
    esp_app_get_elf_sha256(sha, sizeof(sha));
    snprintf(out, out_len, "%s:%s", running ? running->label : "?", sha);
}

static void revert_to_previous(const char *reason)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *previous = esp_ota_get_next_update_partition(NULL); /* the other slot */
    esp_app_desc_t desc;

    if (!previous || previous == running || esp_ota_get_partition_description(previous, &desc) != ESP_OK) {
        ESP_LOGE(TAG, "%s, but there is no valid previous image to go back to — staying", reason);
        return;
    }
    ESP_LOGW(TAG, "%s: going back to %s (version %s)", reason, previous->label, desc.version);
    if (esp_ota_set_boot_partition(previous) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100)); /* let the log drain */
        esp_restart();
    }
    ESP_LOGE(TAG, "could not select %s as boot partition", previous->label);
}

static void confirm_timeout_cb(void *arg)
{
    (void)arg;
    if (!s_confirmed) revert_to_previous("image not confirmed by the host in time");
}

void c6_self_check_on_boot(void)
{
    make_id(s_id, sizeof(s_id));

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    char ok[40] = {0};
    size_t len = sizeof(ok);
    if (nvs_get_str(h, "ok", ok, &len) == ESP_OK && strcmp(ok, s_id) == 0) {
        nvs_close(h);
        ESP_LOGI(TAG, "image %s already confirmed", s_id);
        return;
    }

    char test[40] = {0};
    len = sizeof(test);
    uint8_t tries = 0;
    if (nvs_get_str(h, "test", test, &len) == ESP_OK && strcmp(test, s_id) == 0) {
        nvs_get_u8(h, "tries", &tries);
    } else {
        nvs_set_str(h, "test", s_id);
    }
    tries++;
    nvs_set_u8(h, "tries", tries);
    nvs_commit(h);
    nvs_close(h);

    s_under_test = true;
    ESP_LOGW(TAG, "image %s is under test, boot %u/%u", s_id, tries, C6_SELF_CHECK_MAX_TRIES);
    if (tries > C6_SELF_CHECK_MAX_TRIES) {
        revert_to_previous("image failed to be confirmed on repeated boots");
        return; /* only reached if there is nothing to go back to */
    }

    const esp_timer_create_args_t args = {.callback = confirm_timeout_cb, .name = "c6_selfchk"};
    if (esp_timer_create(&args, &s_timer) == ESP_OK) {
        esp_timer_start_once(s_timer, (uint64_t)C6_SELF_CHECK_CONFIRM_TIMEOUT_MS * 1000ULL);
    }
}

void c6_self_check_confirm(void)
{
    if (s_confirmed) return;
    s_confirmed = true;
    if (s_timer) esp_timer_stop(s_timer);
    if (!s_under_test) return;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ok", s_id);
    nvs_erase_key(h, "test");
    nvs_erase_key(h, "tries");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "image %s confirmed by the host", s_id);
}
