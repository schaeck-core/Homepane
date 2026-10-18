#include "stream_player.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_hw.h"
#include "../../../app/cpp/voice/BandLevels.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "third_party/minimp3/minimp3.h"

static const char *TAG = "stream_player";

#define OUT_FRAMES 512
#define MP3_BUF 8192
#define MP3_DECODE_AT 4096 /* > the largest MP3 frame (1441 bytes) with margin */

typedef enum { FMT_UNKNOWN, FMT_WAV, FMT_MP3 } format_t;

struct stream_player {
    format_t format;
    bool failed;
    char error[80];
    // header sniffing / WAV header
    uint8_t hdr[512];
    size_t hdr_len;
    uint16_t channels;
    uint32_t rate;
    uint8_t partial[4];
    size_t partial_len;
    // MP3
    mp3dec_t mp3;
    uint8_t mp3_in[MP3_BUF];
    size_t mp3_len;
    size_t id3_skip;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    // resampler (16.16 position between prev and cur input sample)
    uint32_t step, pos, step_rate;
    int16_t prev, cur;
    bool have_cur;
    int16_t out[OUT_FRAMES * 2];
    size_t out_n;
};

stream_player_t *stream_player_create(void)
{
    stream_player_t *p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = calloc(1, sizeof(*p));
    if (p) mp3dec_init(&p->mp3);
    return p;
}

void stream_player_destroy(stream_player_t *p) { free(p); }

const char *stream_player_error(const stream_player_t *p) { return p->error; }

static void fail(stream_player_t *p, const char *why)
{
    p->failed = true;
    snprintf(p->error, sizeof(p->error), "%s", why);
    ESP_LOGW(TAG, "%s", why);
}

static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t le32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

static void set_rate(stream_player_t *p, uint32_t rate)
{
    if (rate == p->step_rate) return;
    p->step_rate = rate;
    p->step = (uint32_t)(((uint64_t)rate << 16) / AUDIO_HW_SAMPLE_RATE);
}

static _Atomic float s_level;
static band_levels_t s_bands;

void stream_player_bands(float out[3]) { band_levels_get(&s_bands, out); }

float stream_player_level(void) { return atomic_load(&s_level); }

/* Writes the output block and updates the level (the speaker gets it about
 * as fast as it plays, so the level follows what is heard). */
static void write_out(stream_player_t *p)
{
    double sq = 0;
    for (size_t i = 0; i < p->out_n; ++i) sq += (double)p->out[2 * i] * p->out[2 * i];
    const double db = p->out_n ? 10.0 * log10(sq / p->out_n / (32768.0 * 32768.0) + 1e-12) : -100.0;
    float level = (float)((db + 50.0) / 40.0);
    atomic_store(&s_level, level < 0 ? 0 : level > 1 ? 1 : level);
    int16_t mono[OUT_FRAMES];
    for (size_t i = 0; i < p->out_n; ++i) mono[i] = p->out[2 * i];
    band_levels_feed(&s_bands, mono, p->out_n);
    audio_hw_write(p->out, p->out_n);
    p->out_n = 0;
}

static void emit(stream_player_t *p, int16_t s)
{
    p->prev = p->have_cur ? p->cur : s;
    p->cur = s;
    p->have_cur = true;
    while (p->pos < (1u << 16)) {
        int32_t v = p->prev + (int32_t)(((int64_t)(p->cur - p->prev) * p->pos) >> 16);
        p->out[2 * p->out_n] = p->out[2 * p->out_n + 1] = (int16_t)v;
        if (++p->out_n == OUT_FRAMES) write_out(p);
        p->pos += p->step;
    }
    p->pos -= 1u << 16;
}

static void play_wav_pcm(stream_player_t *p, const uint8_t *data, size_t len)
{
    const size_t fb = p->channels * 2u;
    while (p->partial_len && len) {
        p->partial[p->partial_len++] = *data++;
        --len;
        if (p->partial_len == fb) {
            int16_t a, b = 0;
            memcpy(&a, p->partial, 2);
            if (p->channels == 2) memcpy(&b, p->partial + 2, 2);
            emit(p, p->channels == 2 ? (int16_t)(((int)a + b) / 2) : a);
            p->partial_len = 0;
        }
    }
    size_t frames = len / fb;
    for (size_t i = 0; i < frames; ++i) {
        int16_t a, b = 0;
        memcpy(&a, data + i * fb, 2);
        if (p->channels == 2) memcpy(&b, data + i * fb + 2, 2);
        emit(p, p->channels == 2 ? (int16_t)(((int)a + b) / 2) : a);
    }
    size_t rest = len - frames * fb;
    memcpy(p->partial, data + frames * fb, rest);
    p->partial_len = rest;
}

// Decodes complete frames from mp3_in; keeps an incomplete tail unless `final`.
static void mp3_decode(stream_player_t *p, bool final)
{
    size_t off = 0;
    while (p->mp3_len - off >= (final ? 1 : MP3_DECODE_AT)) {
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&p->mp3, p->mp3_in + off, (int)(p->mp3_len - off), p->pcm, &info);
        if (info.frame_bytes == 0) break; // needs more data (or nothing decodable left)
        off += (size_t)info.frame_bytes;
        if (samples <= 0) continue; // skipped junk/ID3
        set_rate(p, (uint32_t)info.hz);
        for (int i = 0; i < samples; ++i) {
            int16_t s = info.channels == 2 ? (int16_t)(((int)p->pcm[2 * i] + p->pcm[2 * i + 1]) / 2) : p->pcm[i];
            emit(p, s);
        }
    }
    memmove(p->mp3_in, p->mp3_in + off, p->mp3_len - off);
    p->mp3_len -= off;
}

static void feed_mp3(stream_player_t *p, const uint8_t *data, size_t len)
{
    while (len) {
        if (p->id3_skip) {
            size_t n = p->id3_skip < len ? p->id3_skip : len;
            p->id3_skip -= n;
            data += n;
            len -= n;
            continue;
        }
        size_t n = MP3_BUF - p->mp3_len < len ? MP3_BUF - p->mp3_len : len;
        memcpy(p->mp3_in + p->mp3_len, data, n);
        p->mp3_len += n;
        data += n;
        len -= n;
        mp3_decode(p, false);
    }
}

static bool start_wav(stream_player_t *p, const uint8_t *data, size_t len, size_t take)
{
    uint16_t bits = 0;
    for (size_t off = 12; off + 8 <= p->hdr_len;) {
        uint32_t clen = le32(p->hdr + off + 4);
        if (!memcmp(p->hdr + off, "fmt ", 4)) {
            if (off + 8 + 16 > p->hdr_len) return true; // wait for more
            p->channels = le16(p->hdr + off + 10);
            p->rate = le32(p->hdr + off + 12);
            bits = le16(p->hdr + off + 22);
        } else if (!memcmp(p->hdr + off, "data", 4)) {
            if (bits != 16 || p->channels < 1 || p->channels > 2 || p->rate < 8000 || p->rate > 48000) {
                fail(p, "unsupported WAV (need 16-bit PCM, mono/stereo, 8-48 kHz)");
                return false;
            }
            p->format = FMT_WAV;
            set_rate(p, p->rate);
            ESP_LOGI(TAG, "WAV %u Hz, %u ch", (unsigned)p->rate, p->channels);
            size_t data_start = off + 8;
            play_wav_pcm(p, p->hdr + data_start, p->hdr_len - data_start);
            play_wav_pcm(p, data + take, len - take);
            return true;
        }
        off += 8 + clen + (clen & 1);
    }
    if (p->hdr_len == sizeof(p->hdr)) {
        fail(p, "WAV header too long");
        return false;
    }
    return true;
}

bool stream_player_feed(stream_player_t *p, const uint8_t *data, size_t len)
{
    if (p->failed) return false;
    if (p->format == FMT_WAV) {
        play_wav_pcm(p, data, len);
        return true;
    }
    if (p->format == FMT_MP3) {
        feed_mp3(p, data, len);
        return true;
    }
    // Sniff the format from the first bytes.
    size_t take = sizeof(p->hdr) - p->hdr_len < len ? sizeof(p->hdr) - p->hdr_len : len;
    memcpy(p->hdr + p->hdr_len, data, take);
    p->hdr_len += take;
    if (p->hdr_len < 12) return true;
    if (!memcmp(p->hdr, "RIFF", 4) && !memcmp(p->hdr + 8, "WAVE", 4)) return start_wav(p, data, len, take);
    const bool id3 = !memcmp(p->hdr, "ID3", 3);
    const bool sync = p->hdr[0] == 0xFF && (p->hdr[1] & 0xE0) == 0xE0;
    if (id3 || sync) {
        p->format = FMT_MP3;
        ESP_LOGI(TAG, "MP3 stream");
        size_t skip = 0;
        if (id3) {
            // ID3v2: 10-byte header, size as 4 syncsafe bytes (+10 footer if flagged).
            skip = 10 + ((size_t)(p->hdr[6] & 0x7F) << 21 | (size_t)(p->hdr[7] & 0x7F) << 14 |
                         (size_t)(p->hdr[8] & 0x7F) << 7 | (size_t)(p->hdr[9] & 0x7F));
            if (p->hdr[5] & 0x10) skip += 10;
        }
        // Replay everything seen so far through the MP3 path.
        if (skip < p->hdr_len) {
            feed_mp3(p, p->hdr + skip, p->hdr_len - skip);
        } else {
            p->id3_skip = skip - p->hdr_len;
        }
        feed_mp3(p, data + take, len - take);
        return true;
    }
    fail(p, "unknown audio format (neither WAV nor MP3)");
    return false;
}

void stream_player_finish(stream_player_t *p)
{
    if (p->format == FMT_MP3 && !p->failed) mp3_decode(p, true);
    if (p->out_n) write_out(p);
    atomic_store(&s_level, 0.0f);
    band_levels_reset(&s_bands);
}
