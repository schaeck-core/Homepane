#ifndef PLATFORMS_ESP32P4_MAIN_ESPVOICEAUDIO_H
#define PLATFORMS_ESP32P4_MAIN_ESPVOICEAUDIO_H

#include "../../../app/cpp/voice/IVoiceAudio.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace platform_esp32p4 {

class WakeWordDetector;

// IVoiceAudio on audio_hw.c. One microphone task runs for good once started:
// while a capture is active it conditions the audio (high-pass + AGC) and
// hands it to the sink; otherwise it feeds the wake word detector. A
// playback task streams a WAV/MP3 URL (esp_http_client) through
// stream_player.c to the speaker while it is still downloading.
class EspVoiceAudio final : public app::voice::IVoiceAudio {
public:
    EspVoiceAudio();
    ~EspVoiceAudio() override;

    bool start_capture(CaptureSink sink) override;
    void stop_capture() override;
    bool play_url(const std::string& url, PlaybackDone done) override;
    void stop_playback() override;
    float playback_level() const override;
    void playback_bands(float out[3]) const override;
    bool set_wake_word(const std::string& model, std::function<void()> on_detected) override;
    bool play_pcm(std::vector<int16_t> samples) override;
    void set_volume(int percent) override;

private:
    static void mic_task(void* arg);
    static void playback_task(void* arg);
    static void pcm_task(void* arg);
    void run_mic();
    bool ensure_mic_task();

    std::mutex sink_mutex_;
    CaptureSink sink_;                // guarded by sink_mutex_
    std::atomic<uint32_t> session_{0}; // bumped per capture: resets AGC/log
    std::atomic<bool> mic_task_started_{false};

    // Swapped by set_wake_word() under wake_mutex_; the mic task holds the
    // mutex while it uses them.
    std::mutex wake_mutex_;
    std::unique_ptr<WakeWordDetector> detector_;
    std::string wake_model_;
    std::function<void()> on_wake_;
    std::atomic<bool> wake_enabled_{false};

    std::atomic<bool> stop_playback_{false};
    std::atomic<bool> playing_{false};
    std::atomic<int64_t> mute_mic_until_us_{0}; // earcon playing: mic not fed to the sink
    std::atomic<bool> pcm_playing_{false};
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPVOICEAUDIO_H
