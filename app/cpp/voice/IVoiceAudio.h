#ifndef APP_CPP_VOICE_IVOICEAUDIO_H
#define APP_CPP_VOICE_IVOICEAUDIO_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app::voice {

// Platform audio for the voice assistant (docs/voice/VOICE_ASSISTANT_PLAN.md).
// Callbacks run on the platform's audio tasks, never on the UI thread.
class IVoiceAudio {
public:
    // 16 kHz, 16 bit, mono PCM.
    using CaptureSink = std::function<void(const int16_t* samples, size_t count)>;
    using PlaybackDone = std::function<void(bool ok, const std::string& error)>;

    virtual ~IVoiceAudio() = default;

    virtual bool start_capture(CaptureSink sink) = 0;
    // Returns once the sink will not be called again.
    virtual void stop_capture() = 0;

    // Streams `url` (WAV or MP3) to the speaker while it is still being received.
    // `done` fires exactly once, from the playback task.
    virtual bool play_url(const std::string& url, PlaybackDone done) = 0;
    virtual void stop_playback() = 0;

    // Short sound (16 kHz mono), played asynchronously. While it plays (and
    // briefly after) the microphone feeds nothing to the capture sink, so the
    // sound does not end up in the request sent to HA.
    virtual bool play_pcm(std::vector<int16_t> samples)
    {
        (void)samples;
        return false;
    }
    // Speaker volume 0..100.
    virtual void set_volume(int percent) { (void)percent; }
    // Level of the audio playing now, 0..1 (for animations); 0 if unknown.
    virtual float playback_level() const { return 0.0f; }
    // Low / mid / high band levels 0..1 of the playing audio (animations).
    virtual void playback_bands(float out[3]) const { out[0] = out[1] = out[2] = 0.0f; }

    // Listens for the wake word `model` ("okay_nabu", "hey_jarvis"; empty
    // = off) whenever no capture is active and nothing is playing.
    // `on_detected` runs on the audio task: keep it to setting a flag.
    // Returns false if the platform/model is unavailable (then it is off).
    virtual bool set_wake_word(const std::string& model, std::function<void()> on_detected)
    {
        (void)model;
        (void)on_detected;
        return false;
    }
};

} // namespace app::voice

#endif // APP_CPP_VOICE_IVOICEAUDIO_H
