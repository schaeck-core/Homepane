#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "audio_debug.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "audio_hw.h"
#include "stream_player.h"
#include "voice_capture_log.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "lvgl.h"
#include "views/voice_overlay_b.h"
#include "config.h"
#include "cpp/app_facade.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_debug";

#define PORT 8990
#define MAX_SECONDS 10
#define CHUNK_FRAMES 512

// Last recording, interleaved L/R, in PSRAM.
static int16_t *s_rec;
static size_t s_rec_frames;

static int query_int(httpd_req_t *req, const char *key, int def)
{
    char q[96], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return def;
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return def;
    return atoi(v);
}

static bool query_str(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char q[96];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, len) == ESP_OK;
}

static esp_err_t ensure_audio(httpd_req_t *req)
{
    esp_err_t err = audio_hw_init();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "audio init failed");
    }
    return err;
}

static void play_tone(int hz, int ms, int amplitude_pct)
{
    int16_t buf[CHUNK_FRAMES * 2];
    const float amp = 32767.0f * amplitude_pct / 100.0f;
    const size_t total = (size_t)AUDIO_HW_SAMPLE_RATE * ms / 1000;
    const size_t fade = AUDIO_HW_SAMPLE_RATE / 100; // 10 ms ramps, no clicks
    for (size_t done = 0; done < total;) {
        size_t n = total - done < CHUNK_FRAMES ? total - done : CHUNK_FRAMES;
        for (size_t i = 0; i < n; ++i) {
            size_t k = done + i;
            float env = 1.0f;
            if (k < fade) env = (float)k / fade;
            else if (total - k < fade) env = (float)(total - k) / fade;
            int16_t s = (int16_t)(amp * env * sinf(2.0f * (float)M_PI * hz * k / AUDIO_HW_SAMPLE_RATE));
            buf[2 * i] = buf[2 * i + 1] = s;
        }
        audio_hw_write(buf, n);
        done += n;
    }
}

// Per-channel level summary of a recording.
static int describe(const int16_t *lr, size_t frames, char *out, size_t len)
{
    int pos = 0;
    for (int ch = 0; ch < 2; ++ch) {
        double sum = 0, sq = 0;
        int peak = 0;
        for (size_t i = 0; i < frames; ++i) {
            int s = lr[2 * i + ch];
            sum += s;
            sq += (double)s * s;
            if (abs(s) > peak) peak = abs(s);
        }
        double mean = frames ? sum / frames : 0;
        double rms = frames ? sqrt(sq / frames - mean * mean) : 0;
        double dbfs = rms > 0 ? 20.0 * log10(rms / 32768.0) : -120.0;
        pos += snprintf(out + pos, len - pos, "%s: rms %.1f dBFS, peak %d, dc %.0f\n", ch ? "right (MIC2)" : "left  (MIC1)",
                        dbfs, peak, mean);
    }
    return pos;
}

static bool record(size_t frames)
{
    if (!s_rec) s_rec = heap_caps_malloc((size_t)MAX_SECONDS * AUDIO_HW_SAMPLE_RATE * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_rec) return false;
    s_rec_frames = 0;
    while (s_rec_frames < frames) {
        size_t n = frames - s_rec_frames < CHUNK_FRAMES ? frames - s_rec_frames : CHUNK_FRAMES;
        if (audio_hw_read(s_rec + 2 * s_rec_frames, n) != ESP_OK) return false;
        s_rec_frames += n;
    }
    return true;
}

static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static esp_err_t send_wav(httpd_req_t *req)
{
    const uint32_t data_bytes = (uint32_t)(s_rec_frames * 2 * sizeof(int16_t));
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put_le32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16);
    put_le16(h + 20, 1); // PCM
    put_le16(h + 22, 2);
    put_le32(h + 24, AUDIO_HW_SAMPLE_RATE);
    put_le32(h + 28, AUDIO_HW_SAMPLE_RATE * 2 * 2);
    put_le16(h + 32, 4);
    put_le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put_le32(h + 40, data_bytes);
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"recording.wav\"");
    if (httpd_resp_send_chunk(req, (const char *)h, sizeof(h)) != ESP_OK) return ESP_FAIL;
    const char *p = (const char *)s_rec;
    for (uint32_t off = 0; off < data_bytes; off += 4096) {
        uint32_t n = data_bytes - off < 4096 ? data_bytes - off : 4096;
        if (httpd_resp_send_chunk(req, p + off, n) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t tone_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    int hz = query_int(req, "hz", 440), ms = query_int(req, "ms", 1000), amp = query_int(req, "amp", 50);
    play_tone(hz, ms, amp);
    char msg[64];
    snprintf(msg, sizeof(msg), "played %d Hz for %d ms at %d %%\n", hz, ms, amp);
    return httpd_resp_sendstr(req, msg);
}

static esp_err_t record_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    int sec = query_int(req, "sec", 5);
    if (sec < 1) sec = 1;
    if (sec > MAX_SECONDS) sec = MAX_SECONDS;
    ESP_LOGI(TAG, "recording %d s", sec);
    if (!record((size_t)sec * AUDIO_HW_SAMPLE_RATE)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recording failed");
    }
    char stats[160];
    describe(s_rec, s_rec_frames, stats, sizeof(stats));
    ESP_LOGI(TAG, "recorded %u frames\n%s", (unsigned)s_rec_frames, stats);
    return send_wav(req);
}

static esp_err_t last_handler(httpd_req_t *req)
{
    if (!s_rec_frames) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "nothing recorded yet");
    return send_wav(req);
}

static esp_err_t play_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    if (!s_rec_frames) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "nothing recorded yet");
    char ch[8] = "both";
    query_str(req, "ch", ch, sizeof(ch));
    const int pick = !strcmp(ch, "l") ? 0 : !strcmp(ch, "r") ? 1 : -1;
    int16_t buf[CHUNK_FRAMES * 2];
    for (size_t done = 0; done < s_rec_frames;) {
        size_t n = s_rec_frames - done < CHUNK_FRAMES ? s_rec_frames - done : CHUNK_FRAMES;
        for (size_t i = 0; i < n; ++i) {
            const int16_t *f = s_rec + 2 * (done + i);
            int16_t s = pick < 0 ? (int16_t)(((int)f[0] + f[1]) / 2) : f[pick];
            buf[2 * i] = buf[2 * i + 1] = s;
        }
        audio_hw_write(buf, n);
        done += n;
    }
    return httpd_resp_sendstr(req, "played the last recording\n");
}

static esp_err_t stats_handler(httpd_req_t *req)
{
    if (!s_rec_frames) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "nothing recorded yet");
    char stats[160];
    describe(s_rec, s_rec_frames, stats, sizeof(stats));
    return httpd_resp_sendstr(req, stats);
}

static esp_err_t level_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    char msg[64];
    int pos = 0;
    int vol = query_int(req, "vol", -1);
    int gain = query_int(req, "gain", -1);
    if (vol >= 0) {
        audio_hw_set_volume(vol);
        pos += snprintf(msg + pos, sizeof(msg) - pos, "volume %d %%\n", vol);
    }
    if (gain >= 0) {
        audio_hw_set_mic_gain((float)gain);
        pos += snprintf(msg + pos, sizeof(msg) - pos, "mic gain %d dB\n", gain);
    }
    if (!pos) snprintf(msg, sizeof(msg), "use ?vol=0..100 and/or ?gain=0..37\n");
    return httpd_resp_sendstr(req, msg);
}

// POST a WAV (16-bit PCM) or MP3: played while it is received
// (stream_player.c, the same path as the streamed TTS response).
static esp_err_t playwav_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    static uint8_t buf[2048];
    stream_player_t *player = stream_player_create();
    if (!player) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, (char *)buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r <= 0) break;
        remaining -= r;
        if (!stream_player_feed(player, buf, (size_t)r)) {
            esp_err_t e = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, stream_player_error(player));
            stream_player_destroy(player);
            return e;
        }
    }
    stream_player_finish(player);
    stream_player_destroy(player);
    return httpd_resp_sendstr(req, "played\n");
}

// The last voice request's microphone audio, as sent to HA (16 kHz mono).
static esp_err_t voice_wav_handler(httpd_req_t *req)
{
    const int16_t *samples;
    size_t count;
    voice_capture_log_get(&samples, &count);
    if (!count) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no voice request yet");
    // Mono into the stereo recording buffer, so send_wav/describe apply.
    if (!s_rec) s_rec = heap_caps_malloc((size_t)MAX_SECONDS * AUDIO_HW_SAMPLE_RATE * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_rec) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    size_t n = count < (size_t)MAX_SECONDS * AUDIO_HW_SAMPLE_RATE ? count : (size_t)MAX_SECONDS * AUDIO_HW_SAMPLE_RATE;
    for (size_t i = 0; i < n; ++i) s_rec[2 * i] = s_rec[2 * i + 1] = samples[i];
    s_rec_frames = n;
    char stats[160];
    describe(s_rec, s_rec_frames, stats, sizeof(stats));
    ESP_LOGI(TAG, "voice request audio: %u samples\n%s", (unsigned)n, stats);
    return send_wav(req);
}

// Screenshot of the whole display (screen + top layer) as a 24-bit BMP:
// lets UI work be checked without looking at the panel.
static esp_err_t snapshot_handler(httpd_req_t *req)
{
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    lv_draw_buf_t *screen = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
    bsp_display_unlock();
    if (!screen) {
        if (top) lv_draw_buf_destroy(top);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "snapshot failed (memory?)");
    }
    const uint32_t w = screen->header.w, h = screen->header.h;
    const uint32_t row = (w * 3 + 3) & ~3u;
    uint8_t hdr[54] = {'B', 'M'};
    put_le32(hdr + 2, 54 + row * h);
    put_le32(hdr + 10, 54);
    put_le32(hdr + 14, 40);
    put_le32(hdr + 18, w);
    put_le32(hdr + 22, h); // bottom-up
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 24);
    put_le32(hdr + 34, row * h);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr));
    uint8_t *line = malloc(row);
    for (int32_t y = (int32_t)h - 1; line && y >= 0; --y) {
        memset(line, 0, row);
        const uint8_t *s = screen->data + (size_t)y * screen->header.stride; // B, G, R
        const uint8_t *t = top && (uint32_t)y < top->header.h ? top->data + (size_t)y * top->header.stride : NULL;
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t b = s[3 * x], g = s[3 * x + 1], r = s[3 * x + 2];
            if (t && x < top->header.w) { // blend the top layer (B, G, R, A)
                const uint32_t a = t[4 * x + 3];
                b = (t[4 * x] * a + b * (255 - a)) / 255;
                g = (t[4 * x + 1] * a + g * (255 - a)) / 255;
                r = (t[4 * x + 2] * a + r * (255 - a)) / 255;
            }
            line[3 * x] = b;
            line[3 * x + 1] = g;
            line[3 * x + 2] = r;
        }
        if (httpd_resp_send_chunk(req, (const char *)line, row) != ESP_OK) break;
    }
    free(line);
    lv_draw_buf_destroy(screen);
    if (top) lv_draw_buf_destroy(top);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// /ui/voice_b?state=1..5&level=0..100&text=...: force a state of the Aurora
// overlay (state 0 = hide, level -1 = live).
static esp_err_t voice_b_handler(httpd_req_t *req)
{
    int state = query_int(req, "state", 1);
    int level = query_int(req, "level", 50);
    char text[96] = "";
    query_str(req, "text", text, sizeof(text));
    for (char *p = text; *p; ++p) if (*p == '+') *p = ' ';
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    voice_overlay_b_demo((app_cpp_voice_state_t)state, level < 0 ? -1.0f : level / 100.0f, text);
    bsp_display_unlock();
    return httpd_resp_sendstr(req, "ok\n");
}

// /sys/tasks: every FreeRTOS task with state, run time and stack headroom
// (two calls a few seconds apart show who hangs or spins).
static esp_err_t tasks_handler(httpd_req_t *req)
{
    const UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *st = malloc(n * sizeof(TaskStatus_t));
    if (!st) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    uint32_t total = 0;
    const UBaseType_t got = uxTaskGetSystemState(st, n, &total);
    static const char *const states[] = {"running", "ready", "blocked", "suspended", "deleted", "invalid"};
    httpd_resp_set_type(req, "text/plain");
    char line[128];
    for (UBaseType_t i = 0; i < got; ++i) {
        snprintf(line, sizeof(line), "%-18s %-9s prio %2u core %2d runtime %10lu stackfree %5lu %s\n", st[i].pcTaskName,
                 states[st[i].eCurrentState < 5 ? st[i].eCurrentState : 5], (unsigned)st[i].uxCurrentPriority,
                 (int)st[i].xCoreID, (unsigned long)st[i].ulRunTimeCounter, (unsigned long)st[i].usStackHighWaterMark,
                 esp_ptr_internal(st[i].pxStackBase) ? "internal" : "psram");
        httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    }
    free(st);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// /sys/mem: free / lowest free / largest block of internal RAM and PSRAM.
static esp_err_t mem_handler(httpd_req_t *req)
{
    char out[256];
    multi_heap_info_t in, ps;
    heap_caps_get_info(&in, MALLOC_CAP_INTERNAL);
    heap_caps_get_info(&ps, MALLOC_CAP_SPIRAM);
    snprintf(out, sizeof(out),
             "internal: free %u  lowest %u  largest %u  used %u  blocks %u\n"
             "psram:    free %u  lowest %u  largest %u  used %u  blocks %u\n",
             (unsigned)in.total_free_bytes, (unsigned)in.minimum_free_bytes, (unsigned)in.largest_free_block,
             (unsigned)in.total_allocated_bytes, (unsigned)in.allocated_blocks, (unsigned)ps.total_free_bytes,
             (unsigned)ps.minimum_free_bytes, (unsigned)ps.largest_free_block, (unsigned)ps.total_allocated_bytes,
             (unsigned)ps.allocated_blocks);
    return httpd_resp_sendstr(req, out);
}

// /sys/coredump: why the panel last restarted and whether a core dump is
// stored; /sys/coredump.bin: the dump, decoded on a computer with
//   python -m esp_coredump info_corefile --core coredump.bin --core-format raw <the build's .elf>
// (esp_core_dump_get_summary() is not used: on the P4 with code in PSRAM it
// reads the dump through an unmapped flash address and crashes.)
static esp_err_t coredump_handler(httpd_req_t *req)
{
    static const char *const reasons[] = {"unknown", "power-on", "external", "software", "panic", "int-wdt",
                                          "task-wdt", "wdt", "deep-sleep", "brownout", "sdio", "usb", "jtag",
                                          "efuse", "power-glitch", "cpu-lockup"};
    const esp_reset_reason_t r = esp_reset_reason();
    size_t addr = 0, size = 0;
    const bool stored = esp_core_dump_image_get(&addr, &size) == ESP_OK;
    char out[160];
    snprintf(out, sizeof(out), "reset: %s\ncore dump: %s\n",
             (unsigned)r < sizeof(reasons) / sizeof(reasons[0]) ? reasons[r] : "?",
             stored ? "stored (/sys/coredump.bin)" : "none");
    if (stored) {
        char n[32];
        snprintf(n, sizeof(n), "size: %u\n", (unsigned)size);
        strncat(out, n, sizeof(out) - strlen(out) - 1);
    }
    return httpd_resp_sendstr(req, out);
}

static esp_err_t coredump_bin_handler(httpd_req_t *req)
{
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no core dump");
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!part) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no partition");
    httpd_resp_set_type(req, "application/octet-stream");
    uint8_t *buf = malloc(4096);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    for (size_t off = 0; off < size; off += 4096) {
        const size_t len = size - off < 4096 ? size - off : 4096;
        if (esp_partition_read(part, addr - part->address + off, buf, len) != ESP_OK ||
            httpd_resp_send_chunk(req, (const char *)buf, len) != ESP_OK) {
            break;
        }
    }
    free(buf);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// /config/set?key=...&value=...: development shortcut for settings that are
// tedious to type on the panel. Only MQTT keys, the room and the doorbell.
static esp_err_t config_set_handler(httpd_req_t *req)
{
    char q[256], key[32], value[128];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || httpd_query_key_value(q, "key", key, sizeof(key)) != ESP_OK ||
        httpd_query_key_value(q, "value", value, sizeof(value)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need key and value");
    }
    static const char *const allowed[] = {"mqtt_enabled", "mqtt_host",        "mqtt_port",       "mqtt_user",
                                          "mqtt_password", "room",            "room_area_id",    "doorbell_trigger",
                                          "doorbell_camera", "doorbell_unlock"};
    bool ok = false;
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) ok |= strcmp(key, allowed[i]) == 0;
    if (!ok) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "key not allowed");
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    if (strcmp(key, "room_area_id") == 0) {
        const char *name = getConfig("room");
        app_cpp_room_set(value, name ? name : ""); /* set "room" (the name) first */
    } else {
        updateConfig(key, value);
        saveConfig();
    }
    app_cpp_mqtt_apply_settings();
    if (strncmp(key, "doorbell_", 9) == 0) app_cpp_doorbell_apply_settings();
    bsp_display_unlock();
    return httpd_resp_sendstr(req, "ok\n");
}

// /ui/touch?x=..&y=..[&ms=..][&x2=..&y2=..]: a scripted touch (default
// 80 ms; hold with a longer ms; drag to x2/y2), through a virtual pointer
// next to the real panel.
static volatile int32_t s_touch_x, s_touch_y;
static volatile bool s_touch_down;
static lv_indev_t *s_touch_indev;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    data->state = s_touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static esp_err_t touch_handler(httpd_req_t *req)
{
    char q[96], v[12];
    int x = 0, y = 0, ms = 80;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || httpd_query_key_value(q, "x", v, sizeof(v)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need x and y");
    x = atoi(v);
    if (httpd_query_key_value(q, "y", v, sizeof(v)) != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need y");
    y = atoi(v);
    if (httpd_query_key_value(q, "ms", v, sizeof(v)) == ESP_OK) ms = atoi(v);
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    if (!s_touch_indev) {
        s_touch_indev = lv_indev_create();
        lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
    }
    int x2 = x, y2 = y; /* &x2=..&y2=..: drag there over ms */
    if (httpd_query_key_value(q, "x2", v, sizeof(v)) == ESP_OK) x2 = atoi(v);
    if (httpd_query_key_value(q, "y2", v, sizeof(v)) == ESP_OK) y2 = atoi(v);
    s_touch_x = x;
    s_touch_y = y;
    s_touch_down = true;
    bsp_display_unlock();
    if (ms < 40) ms = 40;
    const int steps = ms / 20;
    for (int i = 1; i <= steps; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
        s_touch_x = x + (x2 - x) * i / steps;
        s_touch_y = y + (y2 - y) * i / steps;
    }
    s_touch_down = false;
    vTaskDelay(pdMS_TO_TICKS(60));
    return httpd_resp_sendstr(req, "ok\n");
}

// /voice/button: presses the assistant's microphone button (start/stop).
static esp_err_t voice_button_handler(httpd_req_t *req)
{
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    app_cpp_voice_button();
    bsp_display_unlock();
    return httpd_resp_sendstr(req, "ok\n");
}

typedef struct {
    int hz, ms, amp;
    TaskHandle_t waiter;
} tone_job_t;

static void tone_task(void *arg)
{
    tone_job_t *job = arg;
    vTaskDelay(pdMS_TO_TICKS(200)); // recording starts first: silence, then tone
    play_tone(job->hz, job->ms, job->amp);
    xTaskNotifyGive(job->waiter);
    vTaskDelete(NULL);
}

// Plays a tone while recording: a channel that carries the speaker signal
// at a level independent of the room is an echo reference (for AEC).
static esp_err_t loopback_handler(httpd_req_t *req)
{
    if (ensure_audio(req) != ESP_OK) return ESP_OK;
    tone_job_t job = {query_int(req, "hz", 1000), query_int(req, "ms", 1000), query_int(req, "amp", 40),
                      xTaskGetCurrentTaskHandle()};
    if (xTaskCreate(tone_task, "tone", 4096, &job, 5, NULL) != pdPASS) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no task");
    }
    bool ok = record((size_t)AUDIO_HW_SAMPLE_RATE * (job.ms + 600) / 1000);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!ok) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recording failed");
    char stats[160];
    describe(s_rec, s_rec_frames, stats, sizeof(stats));
    ESP_LOGI(TAG, "loopback\n%s", stats);
    return send_wav(req);
}

/* /ui/perf: display refreshes since the last call (render + transfer of
 * the changed areas): count, average and longest in ms. */
static volatile uint32_t s_refr_count, s_refr_total_us, s_refr_max_us, s_gap_max_us;
static int64_t s_refr_start_us, s_refr_end_us;
/* a timeline of the last frames: start (ms since the first) and duration */
#define TL_MAX 120
static uint32_t s_tl_start[TL_MAX], s_tl_dur[TL_MAX];
static int s_tl_n;
static int64_t s_tl_t0;
static void perf_refr_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_start_us = esp_timer_get_time();
        /* the pause since the last frame (within a movement: < 0.5 s) */
        const int64_t gap = s_refr_start_us - s_refr_end_us;
        if (s_refr_end_us && gap < 500000 && gap > s_gap_max_us) s_gap_max_us = (uint32_t)gap;
    } else if (s_refr_start_us) {
        s_refr_end_us = esp_timer_get_time();
        if (s_tl_n < TL_MAX) {
            if (!s_tl_n) s_tl_t0 = s_refr_start_us;
            s_tl_start[s_tl_n] = (uint32_t)((s_refr_start_us - s_tl_t0) / 1000);
            s_tl_dur[s_tl_n] = (uint32_t)((s_refr_end_us - s_refr_start_us) / 1000);
            s_tl_n++;
        }
        const uint32_t us = (uint32_t)(esp_timer_get_time() - s_refr_start_us);
        s_refr_start_us = 0;
        s_refr_count++;
        s_refr_total_us += us;
        if (us > s_refr_max_us) s_refr_max_us = us;
    }
}

static esp_err_t perf_handler(httpd_req_t *req)
{
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    static bool attached = false;
    if (!attached) {
        lv_display_add_event_cb(lv_display_get_default(), perf_refr_cb, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(lv_display_get_default(), perf_refr_cb, LV_EVENT_REFR_READY, NULL);
        attached = true;
    }
    static char out[3000];
    snprintf(out, sizeof(out), "refreshes %lu avg %.1f ms max %.1f ms, longest pause between two %.1f ms\n",
             (unsigned long)s_refr_count, s_refr_count ? s_refr_total_us / 1000.0 / s_refr_count : 0.0,
             s_refr_max_us / 1000.0, s_gap_max_us / 1000.0);
    uint32_t step_ms = 0;
    const char *step = app_cpp_slowest_step(&step_ms, true);
    const size_t used = strlen(out);
    snprintf(out + used, sizeof(out) - used, "slowest app step: %s %lu ms\n", step, (unsigned long)step_ms);
    if (query_int(req, "timeline", 0)) {
        for (int i = 0; i < s_tl_n; i++) {
            const size_t u = strlen(out);
            if (u + 16 >= sizeof(out)) break;
            snprintf(out + u, sizeof(out) - u, "%lu+%lu ", (unsigned long)s_tl_start[i], (unsigned long)s_tl_dur[i]);
        }
        strncat(out, "\n", sizeof(out) - strlen(out) - 1);
    }
    s_tl_n = 0;
    s_refr_count = s_refr_total_us = s_refr_max_us = s_gap_max_us = 0;
    bsp_display_unlock();
    return httpd_resp_sendstr(req, out);
}

/* /ui/bench?depth=D&n=N: how long a redraw takes. First the whole screen,
 * then each visible object at depth D below the active screen (its area
 * redrawn alone: the object with everything under and over it). Average
 * of N renders each (render + transfer). */
static const char *first_text(lv_obj_t *obj, int depth)
{
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *t = lv_label_get_text(obj);
        if (t && t[0] && (unsigned char)t[0] < 0xE0) return t; /* not an icon */
    }
    if (depth > 6) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        const char *t = first_text(lv_obj_get_child(obj, (int32_t)i), depth + 1);
        if (t) return t;
    }
    return NULL;
}

/* time spent rendering, in the flush callback and waiting for the flush */
static int64_t s_ph_start[3];
static uint64_t s_ph_total[3]; /* render, flush, flush wait (us) */
static void phase_cb(lv_event_t *e)
{
    const int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_RENDER_START: s_ph_start[0] = now; break;
    case LV_EVENT_RENDER_READY: s_ph_total[0] += now - s_ph_start[0]; break;
    case LV_EVENT_FLUSH_START: s_ph_start[1] = now; break;
    case LV_EVENT_FLUSH_FINISH: s_ph_total[1] += now - s_ph_start[1]; break;
    case LV_EVENT_FLUSH_WAIT_START: s_ph_start[2] = now; break;
    case LV_EVENT_FLUSH_WAIT_FINISH: s_ph_total[2] += now - s_ph_start[2]; break;
    default: break;
    }
}

static double render_ms(lv_display_t *disp, const lv_area_t *area, int n)
{
    static bool attached = false;
    if (!attached) {
        const lv_event_code_t codes[] = {LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_START,
                                         LV_EVENT_FLUSH_FINISH, LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH};
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) lv_display_add_event_cb(disp, phase_cb, codes[i], NULL);
        attached = true;
    }
    s_ph_total[0] = s_ph_total[1] = s_ph_total[2] = 0;
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < n; i++) {
        if (area) lv_obj_invalidate_area(lv_screen_active(), area);
        else lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
    }
    return (esp_timer_get_time() - t0) / 1000.0 / n;
}

static void bench_walk(lv_obj_t *obj, int depth, int want, int n, char *out, size_t size)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_area_t vis = {LV_MAX(a.x1, 0), LV_MAX(a.y1, 0), LV_MIN(a.x2, 719), LV_MIN(a.y2, 719)};
    if (vis.x1 > vis.x2 || vis.y1 > vis.y2) return;
    if (depth == want) {
        const size_t used = strlen(out);
        if (used + 120 >= size) return;
        const char *t = first_text(obj, 0);
        snprintf(out + used, size - used, "%7.2f ms  %3d,%3d %3dx%3d  %s\n", render_ms(lv_display_get_default(), &vis, n),
                 (int)vis.x1, (int)vis.y1, (int)lv_area_get_width(&vis), (int)lv_area_get_height(&vis),
                 t ? t : "-");
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        bench_walk(lv_obj_get_child(obj, (int32_t)i), depth + 1, want, n, out, size);
    }
}

static lv_obj_tree_walk_res_t count_cb(lv_obj_t *obj, void *user)
{
    (void)obj;
    ++*(uint32_t *)user;
    return LV_OBJ_TREE_WALK_NEXT;
}

static esp_err_t bench_handler(httpd_req_t *req)
{
    const int depth = query_int(req, "depth", 3);
    const int n = query_int(req, "n", 5);
    static char out[4096];
    if (!bsp_display_lock(5000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    lv_display_t *disp = lv_display_get_default();
    const int w = query_int(req, "w", 0), h = query_int(req, "h", 0);
    if (w > 0 && h > 0) { /* one area: &x=&y=&w=&h= */
        const int x = query_int(req, "x", 0), y = query_int(req, "y", 0);
        const lv_area_t a = {x, y, x + w - 1, y + h - 1};
        const double ms = render_ms(disp, &a, n);
        snprintf(out, sizeof(out), "%7.2f ms  %d,%d %dx%d  (render %.2f, flush %.2f, flush wait %.2f)\n", ms, x, y, w, h,
                 s_ph_total[0] / 1000.0 / n, s_ph_total[1] / 1000.0 / n, s_ph_total[2] / 1000.0 / n);
        bsp_display_unlock();
        return httpd_resp_sendstr(req, out);
    }
    uint32_t objects = 0;
    lv_obj_tree_walk(lv_screen_active(), count_cb, &objects);
    snprintf(out, sizeof(out), "%7.2f ms  whole screen (%lu objects; internal RAM free %u, largest %u)\n",
             render_ms(disp, NULL, n), (unsigned long)objects, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bench_walk(lv_screen_active(), 0, depth, n, out, sizeof(out));
    bsp_display_unlock();
    return httpd_resp_sendstr(req, out);
}

/* /sys/log: the last kLogRing bytes of the log (ESP_LOG and printf via the
 * log's vprintf), kept in PSRAM: what happened before a problem, read
 * without opening the serial port (which restarts the panel). */
#define LOG_RING (32 * 1024)
static char *s_log;
static size_t s_log_pos;   /* next write */
static bool s_log_wrapped;
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_log_next;

static int log_vprintf(const char *fmt, va_list args)
{
    char line[256];
    va_list copy;
    va_copy(copy, args);
    const int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (s_log && n > 0) {
        const size_t len = n < (int)sizeof(line) ? (size_t)n : sizeof(line) - 1;
        portENTER_CRITICAL(&s_log_lock);
        for (size_t i = 0; i < len; i++) {
            s_log[s_log_pos++] = line[i];
            if (s_log_pos == LOG_RING) {
                s_log_pos = 0;
                s_log_wrapped = true;
            }
        }
        portEXIT_CRITICAL(&s_log_lock);
    }
    return s_log_next ? s_log_next(fmt, args) : vprintf(fmt, args);
}

static esp_err_t log_handler(httpd_req_t *req)
{
    if (!s_log) return httpd_resp_sendstr(req, "no log\n");
    char *copy = heap_caps_malloc(LOG_RING, MALLOC_CAP_SPIRAM);
    if (!copy) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    size_t len = 0;
    portENTER_CRITICAL(&s_log_lock);
    if (s_log_wrapped) {
        memcpy(copy, s_log + s_log_pos, LOG_RING - s_log_pos);
        memcpy(copy + (LOG_RING - s_log_pos), s_log, s_log_pos);
        len = LOG_RING;
    } else {
        memcpy(copy, s_log, s_log_pos);
        len = s_log_pos;
    }
    portEXIT_CRITICAL(&s_log_lock);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, copy, (ssize_t)len);
    free(copy);
    return ESP_OK;
}

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
/* /sys/heaptrace?cmd=start: record allocations not freed again (leaks);
 * cmd=dump: those still held in internal RAM, with their callers
 * (addr2line them on the host). A temporary diagnosis. */
#define TRACE_RECORDS 3000
static heap_trace_record_t *s_trace;
static esp_err_t heaptrace_handler(httpd_req_t *req)
{
    char cmd[16] = "";
    query_str(req, "cmd", cmd, sizeof(cmd));
    if (strcmp(cmd, "start") == 0) {
        if (!s_trace) {
            s_trace = heap_caps_calloc(TRACE_RECORDS, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM);
            if (!s_trace || heap_trace_init_standalone(s_trace, TRACE_RECORDS) != ESP_OK)
                return httpd_resp_sendstr(req, "init failed\n");
        }
        heap_trace_stop();
        const esp_err_t err = heap_trace_start(HEAP_TRACE_LEAKS);
        char msg[48];
        snprintf(msg, sizeof(msg), "started: %s\n", esp_err_to_name(err));
        return httpd_resp_sendstr(req, msg);
    }
    httpd_resp_set_type(req, "text/plain");
    const size_t n = heap_trace_get_count();
    char line[256];
    size_t internal_bytes = 0, internal_count = 0;
    for (size_t i = 0; i < n; i++) {
        heap_trace_record_t r;
        if (heap_trace_get(i, &r) != ESP_OK || !r.address || r.freed || !esp_ptr_internal(r.address)) continue;
        internal_bytes += r.size;
        internal_count++;
        int len = snprintf(line, sizeof(line), "%u", (unsigned)r.size);
        for (int d = 0; d < CONFIG_HEAP_TRACING_STACK_DEPTH && len < (int)sizeof(line) - 12; d++) {
            len += snprintf(line + len, sizeof(line) - len, " %p", r.alloced_by[d]);
        }
        snprintf(line + len, sizeof(line) - len, "\n");
        httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    }
    snprintf(line, sizeof(line), "# %u internal allocations held, %u bytes (of %u records)\n", (unsigned)internal_count,
             (unsigned)internal_bytes, (unsigned)n);
    httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}
#endif

/* /sys/sptest?n=N: internal RAM lost by N shared_ptrs made and dropped */
int homepane_shared_ptr_leak_test(int n);
static esp_err_t sptest_handler(httpd_req_t *req)
{
    const int n = query_int(req, "n", 1000);
    char out[96];
    snprintf(out, sizeof(out), "%d shared_ptrs: %d bytes of internal RAM lost\n", n, homepane_shared_ptr_leak_test(n));
    return httpd_resp_sendstr(req, out);
}

/* /sys/cachetest?dir=/sdcard/MACACHE: how long the cover cache's directory
 * walks take on the SD card (FileImageCache::prepare / trim) */
static esp_err_t cachetest_handler(httpd_req_t *req)
{
    char dir[64] = "/sdcard/MACACHE";
    char q[96];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, "dir", dir, sizeof(dir));
    int64_t t0 = esp_timer_get_time();
    int files = 0, statted = 0;
    char first[50][16];
    DIR *d = opendir(dir);
    if (!d) return httpd_resp_sendstr(req, "no such directory\n");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (files < 50) snprintf(first[files], sizeof(first[0]), "%s", e->d_name);
        ++files;
    }
    closedir(d);
    int64_t t1 = esp_timer_get_time();
    for (int i = 0; i < files && i < 50; ++i) {
        char path[96];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, first[i]);
        if (stat(path, &st) == 0) ++statted;
    }
    int64_t t2 = esp_timer_get_time();
    char out[256];
    const double per_stat = statted ? (double)(t2 - t1) / 1000.0 / statted : 0;
    snprintf(out, sizeof(out), "%d files, readdir %.0f ms; stat %.1f ms each (%d); a full trim ~%.1f s\n", files,
             (t1 - t0) / 1000.0, per_stat, statted, (t1 - t0) / 1e6 + per_stat * files / 1000.0);
    return httpd_resp_sendstr(req, out);
}

/* /voice/status: the assistant's state, transcript, answer and last error */
static esp_err_t voice_status_handler(httpd_req_t *req)
{
    static const char *const names[] = {"idle", "listening", "processing", "speaking", "done", "error"};
    app_cpp_voice_status_t st;
    if (!bsp_display_lock(2000)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    app_cpp_voice_get_status(&st);
    const bool enabled = app_cpp_voice_is_enabled();
    bsp_display_unlock();
    char out[1024];
    snprintf(out, sizeof(out), "enabled: %d\nstate: %s\ntranscript: %s\nresponse: %s\nerror: %s\n", enabled,
             names[st.state <= APP_CPP_VOICE_ERROR ? st.state : 0], st.transcript, st.response, st.error);
    return httpd_resp_sendstr(req, out);
}

void audio_debug_start(void)
{
    if (!s_log) {
        s_log = heap_caps_calloc(1, LOG_RING, MALLOC_CAP_SPIRAM);
        if (s_log) s_log_next = esp_log_set_vprintf(log_vprintf);
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = PORT;
    config.ctrl_port = 32769; // the OAuth server (webserver_esp32.c) uses the default
    config.stack_size = 32768; // /audio/playwav decodes MP3 (minimp3: ~16 KB stack scratch)
    config.max_uri_handlers = 28;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "cannot start the audio debug server");
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/audio/tone", .method = HTTP_GET, .handler = tone_handler},
        {.uri = "/audio/record", .method = HTTP_GET, .handler = record_handler},
        {.uri = "/audio/last.wav", .method = HTTP_GET, .handler = last_handler},
        {.uri = "/audio/play", .method = HTTP_GET, .handler = play_handler},
        {.uri = "/audio/stats", .method = HTTP_GET, .handler = stats_handler},
        {.uri = "/audio/level", .method = HTTP_GET, .handler = level_handler},
        {.uri = "/audio/loopback", .method = HTTP_GET, .handler = loopback_handler},
        {.uri = "/audio/playwav", .method = HTTP_POST, .handler = playwav_handler},
        {.uri = "/audio/voice.wav", .method = HTTP_GET, .handler = voice_wav_handler},
        {.uri = "/ui/snapshot.bmp", .method = HTTP_GET, .handler = snapshot_handler},
        {.uri = "/ui/voice_b", .method = HTTP_GET, .handler = voice_b_handler},
        {.uri = "/voice/button", .method = HTTP_GET, .handler = voice_button_handler},
        {.uri = "/sys/tasks", .method = HTTP_GET, .handler = tasks_handler},
        {.uri = "/config/set", .method = HTTP_GET, .handler = config_set_handler},
        {.uri = "/ui/touch", .method = HTTP_GET, .handler = touch_handler},
        {.uri = "/ui/perf", .method = HTTP_GET, .handler = perf_handler},
        {.uri = "/sys/mem", .method = HTTP_GET, .handler = mem_handler},
        {.uri = "/sys/log", .method = HTTP_GET, .handler = log_handler},
        {.uri = "/sys/coredump", .method = HTTP_GET, .handler = coredump_handler},
        {.uri = "/sys/coredump.bin", .method = HTTP_GET, .handler = coredump_bin_handler},
        {.uri = "/sys/sptest", .method = HTTP_GET, .handler = sptest_handler},
        {.uri = "/sys/cachetest", .method = HTTP_GET, .handler = cachetest_handler},
        {.uri = "/voice/status", .method = HTTP_GET, .handler = voice_status_handler},
#if CONFIG_HEAP_TRACING_STANDALONE
        {.uri = "/sys/heaptrace", .method = HTTP_GET, .handler = heaptrace_handler},
#endif
        {.uri = "/ui/bench", .method = HTTP_GET, .handler = bench_handler},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) httpd_register_uri_handler(server, &uris[i]);
    ESP_LOGW(TAG, "AUDIO DEBUG SERVER on port %d (developer build only; exposes the microphones)", PORT);
}
