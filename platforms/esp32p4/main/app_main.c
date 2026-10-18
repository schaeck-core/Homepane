#include "lvgl/lvgl.h"
#include "config.h"
#include "i18n.h"
#include "network.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cjson/cJSON.h>
#include "esp_timer.h"
#include "lvgl.h"
// ESP-IDF LVGL port helper
#include "esp_lvgl_port.h"
// Networking & Ethernet
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"

// Waveshare BSP headers (from components/esp32_p4_wifi6_touch_lcd_4b/include)
#include "bsp/display.h"
#include "bsp/touch.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include <string.h>
// LittleFS mount (ESP-IDF Component Manager: joltwallet/littlefs)
#include "esp_littlefs.h"
// SDMMC + FATFS for SD card storage
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "slave_ota.h"
#include "EspImprovSerial.h"
#include "views/voice_overlay.h"
#ifdef HOMEPANE_AUDIO_DEBUG
#include "audio_debug.h"
#endif
#include "nvs_flash.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include "app/cpp/app_facade.h"

// Track U Phase U0 (CPP_MIGRATION_PLAN.md §19): version string + rollback
// self-test confirmation.
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"

void mount_fs(void) {
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE("littlefs", "Failed to mount LittleFS (%s)", esp_err_to_name(err));
    } else {
        size_t total = 0, used = 0;
        if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
            ESP_LOGI("littlefs", "Mounted at %s, total=%u, used=%u", conf.base_path, (unsigned)total, (unsigned)used);
        }
    }
}

#define CONFIG_INTERNAL_PATH "/littlefs/config.ini"
#define CONFIG_INTERNAL_TMP  "/littlefs/config.tmp"
#define CONFIG_SD_PATH       "/sdcard/config.ini"
#define CONFIG_SD_IMPORTED   "/sdcard/config.imp" /* 8.3 names only (no FatFs LFN) */

/* The config (WiFi password, HA token) lives in internal flash: an SD card
 * is removable and readable anywhere. A config.ini put on the card is a
 * deliberate provisioning step: it replaces the internal config and is then
 * renamed, so it is neither imported again nor mistaken for the live config. */
static void import_sd_config(void)
{
    struct stat st;
    if (stat(CONFIG_SD_PATH, &st) != 0) return;
    FILE *in = fopen(CONFIG_SD_PATH, "rb");
    FILE *out = in ? fopen(CONFIG_INTERNAL_TMP, "wb") : NULL;
    bool ok = in && out;
    char buf[512];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
        ok = fwrite(buf, 1, n, out) == n;
    }
    if (in) ok = ok && !ferror(in);
    if (in) fclose(in);
    if (out && fclose(out) != 0) ok = false;
    /* Replace the internal config only with a complete copy. */
    if (ok) ok = rename(CONFIG_INTERNAL_TMP, CONFIG_INTERNAL_PATH) == 0;
    if (!ok) {
        remove(CONFIG_INTERNAL_TMP);
        ESP_LOGE("storage", "Importing %s failed; internal config and card left as they were", CONFIG_SD_PATH);
        return;
    }
    remove(CONFIG_SD_IMPORTED);
    if (rename(CONFIG_SD_PATH, CONFIG_SD_IMPORTED) == 0) {
        ESP_LOGW("storage", "Imported %s into internal flash; renamed it to %s (contains credentials, delete it)",
                 CONFIG_SD_PATH, CONFIG_SD_IMPORTED);
    } else {
        /* Left in place it would be imported again on every boot,
         * overwriting later changes. The data is in internal flash now. */
        remove(CONFIG_SD_PATH);
        ESP_LOGW("storage", "Imported %s into internal flash and deleted it (rename failed)", CONFIG_SD_PATH);
    }
}

static bool mount_sdcard(void) {
    // Use the official Waveshare BSP helper, which mounts the uSD
    // on SDMMC host slot 0 (separate from the WiFi SDIO slot 1).
    esp_err_t ret = bsp_sdcard_mount();

    if (ret == ESP_OK) {
        if (bsp_sdcard) {
            sdmmc_card_print_info(stdout, bsp_sdcard);
        }
        ESP_LOGI("sdmmc", "Mounted SD card via BSP.");
        return true;
    }

    ESP_LOGW("sdmmc", "SD card mount failed: %s", esp_err_to_name(ret));
    return false;
}

static const char *TAG = "app_main";

static void *json_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

static volatile bool s_low_power = false;

/* ── Power save helpers ─────────────────────────────────────── */

/* Time without touch until the screen goes off (Settings › Screensaver):
 * 0 = never. Never before the screensaver. */
static uint32_t get_screen_timeout_ms(void)
{
    return (uint32_t)app_cpp_screen_off_after_s() * 1000;
}

static void power_save_enter(lv_display_t *disp)
{
    if (s_low_power) return;
    s_low_power = true;

    /* 1. Turn off backlight to save power. */
    bsp_display_backlight_off();

    /* 2. Slow down rendering to ~2 FPS – saves CPU while screen is off. */
    if (disp) {
        lv_timer_t *refr = lv_display_get_refr_timer(disp);
        if (refr) lv_timer_set_period(refr, 500);
    }

    ESP_LOGI(TAG, "Power save: entered (brightness 10, render slowed)");
}

static void power_save_exit(lv_display_t *disp)
{
    if (!s_low_power) return;
    s_low_power = false;

    /* 1. Restore full render speed and force redraw. */
    if (disp) {
        lv_timer_t *refr = lv_display_get_refr_timer(disp);
        if (refr) lv_timer_set_period(refr, 5);
        lv_obj_invalidate(lv_screen_active());
    }

    /* 2. Turn backlight back on and restore active brightness */
    bsp_display_backlight_on();
    bsp_display_brightness_set(50);

    ESP_LOGI(TAG, "Power save: exited (brightness 50, render restored)");
}

static void wake_if_needed(lv_event_t *e)
{
    if (s_low_power) {
        lv_display_t *disp = lv_display_get_default();
        power_save_exit(disp);
        if (disp) lv_disp_trig_activity(disp);
        ESP_LOGI(TAG, "Woke from low power due to touch");
    } else {
        bsp_display_brightness_set(50);
    }
}

/* The backlight is active-low behind an inverted LEDC channel: duty 0 keeps
 * GPIO26 high, i.e. really off (bsp_display_backlight_off() only dims to the
 * BSP's 47 % floor). Switched off before every esp_restart(), so the panel
 * does not light up while it stops getting a picture (blue flash).
 *
 * Latching the pin through the reset (gpio_hold_en) would also cover the
 * bootloader phase, but a firmware that boots afterwards without releasing
 * the latch would stay dark — older images (rollback!) do not. app_main()
 * already releases it, so hold can be enabled once all installed images do. */
static void backlight_off_for_restart(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH);
    esp_rom_delay_us(1000); /* > one 5 kHz PWM period, so the new duty is out */
}

void app_main(void)
{
    // Track U Phase U0 (CPP_MIGRATION_PLAN.md §19): make the running
    // firmware version queryable at runtime, from version.txt (NOT git
    // describe — this repo's tags are inherited from upstream LVGL
    // history and would embed a misleading version otherwise).
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware version: %s", app_desc->version);

    // Phase 1 of CPP_MIGRATION_PLAN.md: construct the C++ Application
    // skeleton early. It does not touch hardware/network yet.
    app_cpp_create();

    // Initialize NVS (needed by esp_hosted RPC)
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize event loop and network stack before starting any servers
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    // Wi-Fi credentials from the web flasher (Improv over the USB port). As
    // early as possible: the browser asks right after the reset.
    improv_serial_start();

    // Note: Ethernet/WiFi will be initialized through the network_setup UI
    // in the initialization view, not here at startup
    // Initialize display via Waveshare BSP using the proven pattern
    bsp_display_cfg_t cfg = (bsp_display_cfg_t){
        .lvgl_port_cfg = (lvgl_port_cfg_t){
            .task_priority = 4,
            .task_stack = 24576, // 16 KB had only ~4 KB left with big pages (the music search): crashes
            .task_affinity = -1,
            .task_max_sleep_ms = 500, // target ~2 FPS max
            .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
            .timer_period_ms = 5,
        },
        .buffer_size = 720*720,
        .double_buffer = 1,
        .flags = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
            .buff_dma = false, // esp_lvgl_port: DMA buffers only for RGB565
#else
            .buff_dma = true,
#endif
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };

    bsp_display_start_with_config(&cfg);
    /* LEDC now keeps the backlight off itself (duty 0); release a pad latch
     * a future image may set before restarting (see backlight_off_for_restart). */
    gpio_hold_dis(BSP_LCD_BACKLIGHT);
    esp_register_shutdown_handler(backlight_off_for_restart);

    // Paint a dark background before the backlight comes on so the user never
    // sees the white default LVGL canvas during the 10-15 s OTA-check phase.
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);
    bsp_display_unlock();

    // Mount filesystems early — OTA check reads firmware from LittleFS
    mount_fs();
    if (mount_sdcard()) {
        import_sd_config();
    }
    set_config_path(CONFIG_INTERNAL_PATH);
    ESP_LOGI("storage", "Config at %s", CONFIG_INTERNAL_PATH);

    // Render app UI from src/ using existing views
    bsp_display_lock(0);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);

    // loadConfig() must run before network_auto_connect_async() (reads
    // saved wifi_ssid/wifi_password) and before
    // app_cpp_enter_initial_state() (reads haas_token/haas_dashboard) —
    // see AppStateController.h's own comment on this ordering constraint
    // (CPP_MIGRATION_PLAN.md Phase 11).
    /* JSON trees (Home Assistant messages, often large) in PSRAM, like the
     * app's C++ data (PsramNew.cpp): internal RAM is the scarce one. */
    static cJSON_Hooks json_hooks = {.malloc_fn = json_malloc, .free_fn = heap_caps_free};
    cJSON_InitHooks(&json_hooks);
    loadConfig();
    slave_ota_load_settings();
    { /* UI language (app/i18n); German until the user picks another */
        const char *lang = getConfig("language");
        i18n_set_language(lang && lang[0] ? lang : "de");
    }

    // Start WiFi early — non-blocking. This ensures the network stack is
    // up regardless of which app state follows (init, setup, or
    // dashboard). If no saved credentials exist this is a no-op.
    network_auto_connect_async();

    // Shared app-state logic (error handler, fonts, bg, Init/Setup/Complete
    // decision) — CPP_MIGRATION_PLAN.md Phase 11.
    app_cpp_enter_initial_state();
    bsp_display_unlock();

    app_cpp_start();

    // Voice assistant (docs/voice/VOICE_ASSISTANT_PLAN.md): microphone
    // button + overlay; on/off, wake word and HA assistant come from the
    // settings (off by default).
    bsp_display_lock(0);
    voice_overlay_init();
    bsp_display_unlock();
#ifdef HOMEPANE_AUDIO_DEBUG
    audio_debug_start();
#endif

    // Render and flush the first frame synchronously, so the panel shows it
    // (not its power-on content) when the backlight comes on.
    bsp_display_lock(0);
    lv_refr_now(NULL);
    bsp_display_unlock();

    // Turn on the backlight now – the first frame (splash or dashboard) is ready.
    bsp_display_backlight_on();
    bsp_display_brightness_set(50);

    // Track U Phase U0 (CPP_MIGRATION_PLAN.md §19.3): confirm this OTA
    // slot as valid, cancelling the bootloader's automatic-rollback timer.
    // Deliberately minimal for now — "a real frame rendered" rather than
    // just "code executed without crashing" — but not yet Phase U1's
    // fuller self-test (HA/MQTT-ready, timeout-bounded). Without *some*
    // confirmation here, every boot from an OTA slot would eventually
    // roll back, since nothing would ever clear the pending-verify flag.
    esp_ota_mark_app_valid_cancel_rollback();

    // Only now — with this P4 image confirmed — update the ESP32-C6
    // co-processor from the c6fw partition if that image is newer. Shows a
    // progress overlay and restarts on success (which also resets the C6
    // into the new image); otherwise returns quickly.
    slave_ota_update_if_needed();

    // Register a global touch handler to wake from low power
    lv_obj_add_event_cb(lv_screen_active(), wake_if_needed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(lv_screen_active(), wake_if_needed, LV_EVENT_CLICKED, NULL);

    // Log filesystem usage after potential writes during init
#ifdef ESP_PLATFORM
    size_t total = 0, used = 0;
    if (esp_littlefs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI("littlefs", "Post-init FS usage: total=%u, used=%u", (unsigned)total, (unsigned)used);
    }
#endif

    lv_display_t *disp = lv_display_get_default();
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);

    uint32_t timeout_ms = get_screen_timeout_ms();
    ESP_LOGI(TAG, "Screen timeout: %lu ms", (unsigned long)timeout_ms);
    uint32_t timeout_read_at = 0;

    // Main loop; LVGL task is handled by the port
    while (true) {
        if (!disp) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // The whole C++ application (network event delivery, HaClient,
        // dashboard/widget creation and updates, UiDispatcher commands)
        // touches LVGL and is not thread-safe, while LVGL itself runs on
        // esp_lvgl_port's task (timers, input callbacks — which also call
        // into the C++ app). Holding the (recursive) LVGL mutex here
        // serializes the two, i.e. makes the app effectively
        // single-threaded like on the simulator.
        // The power-save checks below call LVGL too (inactive time,
        // refresh timer, invalidate), so they share the same lock.
        if (bsp_display_lock(0)) {
            app_cpp_poll();

            uint32_t idle_ms = lv_disp_get_inactive_time(disp);
            /* the setting may change at any time: read it once a second */
            if (lv_tick_elaps(timeout_read_at) >= 1000) {
                timeout_read_at = lv_tick_get();
                const uint32_t now_ms = get_screen_timeout_ms();
                if (now_ms != timeout_ms) ESP_LOGI(TAG, "Screen timeout: %lu ms", (unsigned long)now_ms);
                timeout_ms = now_ms;
            }

            if (!s_low_power && timeout_ms && idle_ms >= timeout_ms) {
                power_save_enter(disp);
            } else if (s_low_power && (!timeout_ms || idle_ms < timeout_ms)) {
                /* Touch was registered by LVGL (idle reset) – wake up */
                power_save_exit(disp);
            }

            bsp_display_unlock();
        }

        /* Poll less often while the screen is off. 20 ms while on: results
         * of background work (camera pictures, network replies) reach the UI
         * in time — at 200 ms a camera stream showed ~3 pictures/s. */
        vTaskDelay(pdMS_TO_TICKS(s_low_power ? 500 : 20));
    }
}
