#include "EspVoiceAudio.h"

#include "../../../app/cpp/voice/SpeechAgc.h"
#include "WakeWordDetector.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" {
#include "audio_hw.h"
#include "esp_heap_caps.h"
#include "stream_player.h"
#include "voice_capture_log.h"
}

namespace platform_esp32p4 {

namespace {
const char* TAG = "voice_audio";
constexpr size_t kCaptureFrames = 320; // 20 ms at 16 kHz
constexpr size_t kLogSamples = 16000 * 15;
// Dropped after the start: the tap on the panel and the I2S start-up land
// here, nobody is talking yet.
constexpr int kSkipBlocks = 8; // 160 ms

// Last request's microphone audio (PSRAM), see voice_capture_log.h.
int16_t* g_log;
size_t g_log_count;

struct PlaybackJob {
    EspVoiceAudio* self;
    std::string url;
    app::voice::IVoiceAudio::PlaybackDone done;
    std::atomic<bool>* stop;
    std::atomic<bool>* playing;
};
} // namespace

extern "C" {
extern const uint8_t okay_nabu_tflite_start[] asm("_binary_okay_nabu_tflite_start");
extern const uint8_t hey_jarvis_tflite_start[] asm("_binary_hey_jarvis_tflite_start");
}

EspVoiceAudio::EspVoiceAudio() = default;
EspVoiceAudio::~EspVoiceAudio() = default;

bool EspVoiceAudio::ensure_mic_task()
{
    if (audio_hw_init() != ESP_OK) return false;
    bool expected = false;
    if (!mic_task_started_.compare_exchange_strong(expected, true)) return true;
    // Core 1, above the UI: dropped microphone audio is worse than a late
    // frame. TFLM inference needs a few KB of stack; PSRAM keeps internal
    // RAM free.
    if (xTaskCreatePinnedToCoreWithCaps(mic_task, "voice_mic", 16384, this, 6, nullptr, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        mic_task_started_ = false;
        return false;
    }
    return true;
}

bool EspVoiceAudio::start_capture(CaptureSink sink)
{
    {
        std::lock_guard<std::mutex> lock(sink_mutex_);
        sink_ = std::move(sink);
        ++session_;
    }
    return ensure_mic_task();
}

void EspVoiceAudio::stop_capture()
{
    // The mic task calls the sink only under this mutex: once cleared here,
    // it is never called again (the contract in IVoiceAudio).
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_ = nullptr;
}

bool EspVoiceAudio::set_wake_word(const std::string& model, std::function<void()> on_detected)
{
    // Values from main/wakeword/<model>.json.
    WakeWordDetector::Model m{};
    if (model == "okay_nabu") m = {okay_nabu_tflite_start, 0.97f, 26080, "Okay Nabu"};
    else if (model == "hey_jarvis") m = {hey_jarvis_tflite_start, 0.97f, 22860, "Hey Jarvis"};

    std::lock_guard<std::mutex> lock(wake_mutex_);
    if (!m.tflite) {
        wake_enabled_ = false;
        detector_.reset();
        wake_model_.clear();
        if (!model.empty()) ESP_LOGW(TAG, "unknown wake word '%s'", model.c_str());
        return model.empty();
    }
    if (model != wake_model_ || !detector_) {
        std::unique_ptr<WakeWordDetector> d(new WakeWordDetector);
        if (!d->init(m)) {
            wake_enabled_ = false;
            detector_.reset();
            wake_model_.clear();
            return false;
        }
        detector_ = std::move(d);
        wake_model_ = model;
    }
    on_wake_ = std::move(on_detected);
    wake_enabled_ = true;
    return ensure_mic_task();
}

void EspVoiceAudio::mic_task(void* arg)
{
    static_cast<EspVoiceAudio*>(arg)->run_mic(); // never returns
}

namespace {
void run_playback(PlaybackJob* job);

// The detector sees the raw microphones (its PCAN stage normalizes
// level itself); a fixed boost brings arm's-length speech (~-40 dBFS) to the
// level range the models saw in training.
constexpr int kWakeGain = 4;
} // namespace

void EspVoiceAudio::run_mic()
{
    std::unique_ptr<int16_t[]> stereo(new int16_t[kCaptureFrames * 2]);
    std::unique_ptr<int16_t[]> mono(new int16_t[kCaptureFrames]);
    std::unique_ptr<int16_t[]> wake(new int16_t[kCaptureFrames]);
    if (!g_log) g_log = static_cast<int16_t*>(heap_caps_malloc(kLogSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    std::unique_ptr<app::voice::SpeechAgc> agc(new app::voice::SpeechAgc(AUDIO_HW_SAMPLE_RATE));
    uint32_t seen_session = session_.load();
    int skip = 0;
    bool was_capturing = false;
    float max_prob = 0;
    int64_t last_report = esp_timer_get_time();

    for (;;) {
        bool needed;
        {
            std::lock_guard<std::mutex> lock(sink_mutex_);
            needed = static_cast<bool>(sink_) || wake_enabled_;
        }
        if (!needed) {
            // Nothing listens (microphone/assistant off): do not even read
            // the microphones.
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (audio_hw_read(stereo.get(), kCaptureFrames) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        // Both microphones sit side by side; averaging gains ~3 dB SNR on
        // uncorrelated noise without any beamforming.
        for (size_t i = 0; i < kCaptureFrames; ++i) {
            mono[i] = static_cast<int16_t>((static_cast<int>(stereo[2 * i]) + stereo[2 * i + 1]) / 2);
        }

        bool capturing;
        {
            std::lock_guard<std::mutex> lock(sink_mutex_);
            capturing = static_cast<bool>(sink_);
            // Our own earcon must not reach HA (nor the AGC / endpointer).
            if (capturing && esp_timer_get_time() >= mute_mic_until_us_.load()) {
                if (session_.load() != seen_session) {
                    seen_session = session_.load();
                    agc->reset();
                    g_log_count = 0;
                    skip = kSkipBlocks;
                }
                if (skip > 0) {
                    --skip;
                } else {
                    agc->process(mono.get(), kCaptureFrames);
                    if (g_log && g_log_count + kCaptureFrames <= kLogSamples) {
                        std::memcpy(g_log + g_log_count, mono.get(), kCaptureFrames * sizeof(int16_t));
                        g_log_count += kCaptureFrames;
                    }
                    sink_(mono.get(), kCaptureFrames);
                }
            }
        }

        std::lock_guard<std::mutex> wake_lock(wake_mutex_);
        if (detector_ && wake_enabled_) {
            if (capturing || playing_) {
                was_capturing = true; // our own voice request or the response: no wake word
            } else {
                if (was_capturing) {
                    detector_->reset();
                    was_capturing = false;
                }
                for (size_t i = 0; i < kCaptureFrames; ++i) {
                    wake[i] = static_cast<int16_t>(std::clamp(mono[i] * kWakeGain, -32768, 32767));
                }
                if (detector_->process(wake.get(), kCaptureFrames)) {
                    ESP_LOGI(TAG, "wake word detected (p=%.2f)", detector_->last_probability());
                    if (on_wake_) on_wake_();
                }
                max_prob = std::max(max_prob, detector_->last_probability());
            }
            const int64_t now = esp_timer_get_time();
            if (now - last_report > 30 * 1000000LL) {
                ESP_LOGI(TAG, "wake word: inference %u us avg, max probability %.2f in the last 30 s",
                         static_cast<unsigned>(detector_->avg_invoke_us()), max_prob);
                max_prob = 0;
                last_report = now;
            }
        }
    }
}

bool EspVoiceAudio::play_url(const std::string& url, PlaybackDone done)
{
    if (audio_hw_init() != ESP_OK) return false;
    stop_playback();
    stop_playback_ = false;
    playing_ = true;
    auto* job = new PlaybackJob{this, url, std::move(done), &stop_playback_, &playing_};
    // minimp3 keeps ~16 KB of scratch on the stack, TLS (https HA URLs)
    // needs more on top: 32 KB, in PSRAM so internal RAM stays free.
    if (xTaskCreatePinnedToCoreWithCaps(playback_task, "voice_play", 32768, job, 6, nullptr, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        playing_ = false;
        delete job;
        return false;
    }
    return true;
}

namespace {
struct PcmJob {
    EspVoiceAudio* self;
    std::vector<int16_t> samples;
    std::atomic<bool>* playing;
};
} // namespace

bool EspVoiceAudio::play_pcm(std::vector<int16_t> samples)
{
    if (samples.empty() || audio_hw_init() != ESP_OK) return false;
    bool expected = false;
    if (!pcm_playing_.compare_exchange_strong(expected, true)) return false; // one at a time
    // Mic muted for the sound plus ~150 ms of room echo.
    mute_mic_until_us_ = esp_timer_get_time() + static_cast<int64_t>(samples.size()) * 1000000 / 16000 + 150000;
    auto* job = new PcmJob{this, std::move(samples), &pcm_playing_};
    if (xTaskCreatePinnedToCoreWithCaps(pcm_task, "voice_pcm", 4096, job, 6, nullptr, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        pcm_playing_ = false;
        delete job;
        return false;
    }
    return true;
}

void EspVoiceAudio::pcm_task(void* arg)
{
    {
        std::unique_ptr<PcmJob> job(static_cast<PcmJob*>(arg));
        int16_t stereo[2 * 256];
        for (size_t off = 0; off < job->samples.size(); off += 256) {
            const size_t n = std::min<size_t>(256, job->samples.size() - off);
            for (size_t i = 0; i < n; ++i) stereo[2 * i] = stereo[2 * i + 1] = job->samples[off + i];
            audio_hw_write(stereo, n);
        }
        *job->playing = false;
    }
    vTaskDeleteWithCaps(nullptr);
}

void EspVoiceAudio::set_volume(int percent)
{
    if (audio_hw_init() == ESP_OK) audio_hw_set_volume(percent);
}

float EspVoiceAudio::playback_level() const { return playing_ ? stream_player_level() : 0.0f; }

void EspVoiceAudio::playback_bands(float out[3]) const
{
    if (playing_) stream_player_bands(out);
    else out[0] = out[1] = out[2] = 0.0f;
}

void EspVoiceAudio::stop_playback()
{
    stop_playback_ = true;
    while (playing_) vTaskDelay(pdMS_TO_TICKS(5));
}

void EspVoiceAudio::playback_task(void* arg)
{
    auto* job = static_cast<PlaybackJob*>(arg);
    std::atomic<bool>* playing = job->playing;
    run_playback(job); // deletes job
    *playing = false;
    vTaskDeleteWithCaps(nullptr);
}

namespace {
void run_playback(PlaybackJob* raw)
{
    std::unique_ptr<PlaybackJob> job(raw);
    std::string error;
    bool ok = false;

    esp_http_client_config_t cfg = {};
    cfg.url = job->url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    std::unique_ptr<stream_player_t, void (*)(stream_player_t*)> player(stream_player_create(), stream_player_destroy);
    const int64_t t0 = esp_timer_get_time();
    bool first = true;

    if (!player) {
        error = "out of memory";
    } else if (!client) {
        error = "http init failed";
    } else if (esp_http_client_open(client, 0) != ESP_OK) {
        error = "cannot connect to Home Assistant";
    } else {
        esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            error = "HTTP " + std::to_string(status);
        } else {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[2048]);
            ok = true;
            while (!*job->stop) {
                int n = esp_http_client_read(client, reinterpret_cast<char*>(buf.get()), 2048);
                if (n < 0) {
                    ok = false;
                    error = "download interrupted";
                    break;
                }
                if (n == 0) break;
                if (first) {
                    ESP_LOGI(TAG, "first response audio after %lld ms", (esp_timer_get_time() - t0) / 1000);
                    first = false;
                }
                if (!stream_player_feed(player.get(), buf.get(), static_cast<size_t>(n))) {
                    ok = false;
                    error = stream_player_error(player.get());
                    break;
                }
            }
            stream_player_finish(player.get());
        }
    }
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (*job->stop) {
        ok = false;
        error = "stopped";
    }
    if (job->done) job->done(ok, error);
}
} // namespace

} // namespace platform_esp32p4

extern "C" void voice_capture_log_get(const int16_t** samples, size_t* count)
{
    *samples = platform_esp32p4::g_log;
    *count = platform_esp32p4::g_log ? platform_esp32p4::g_log_count : 0;
}
