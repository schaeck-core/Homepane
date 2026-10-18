#ifndef APP_CPP_VOICE_SPEECHENDPOINTER_H
#define APP_CPP_VOICE_SPEECHENDPOINTER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace app::voice {

// Decides on the device when the user has finished speaking, also with a TV
// or radio talking in the background (where HA's voice activity detection
// hears "speech" forever). The person addressing the panel is close to it
// and much louder than background sound: once the level stays well below
// the user's own speech level for a while, the request is over.
// Fed with consecutive blocks (e.g. 20 ms) of the audio sent to HA.
class SpeechEndpointer {
public:
    static constexpr float kStartAboveNoiseDb = 12.0f; // speech begins
    static constexpr float kEndBelowSpeechDb = 15.0f;  // "quiet" relative to the user's speech
    static constexpr uint32_t kMinSpeechMs = 300;      // ignore clicks/short noises
    static constexpr uint32_t kEndSilenceMs = 800;     // quiet this long = finished

    void reset()
    {
        noise_db_ = 0;
        have_noise_ = false;
        speech_db_ = -120;
        speech_ms_ = 0;
        quiet_ms_ = 0;
        ended_ = false;
    }

    // Returns true once (the block in which the end is detected).
    bool feed(const int16_t* samples, size_t count, uint32_t sample_rate = 16000)
    {
        if (ended_ || !count) return false;
        double sq = 0;
        for (size_t i = 0; i < count; ++i) sq += static_cast<double>(samples[i]) * samples[i];
        const float db = static_cast<float>(10.0 * std::log10(sq / count / (32768.0 * 32768.0) + 1e-12));
        const uint32_t ms = static_cast<uint32_t>(count * 1000 / sample_rate);

        // Noise floor: lowest level seen, creeping up slowly.
        if (!have_noise_ || db < noise_db_) noise_db_ = db;
        else noise_db_ += 0.01f;
        have_noise_ = true;

        if (speech_ms_ == 0 && db < noise_db_ + kStartAboveNoiseDb) return false; // not started yet
        // Speech level: loudest recent block, decaying ~1 dB/s.
        speech_db_ = std::max(db, speech_db_ - 0.02f * ms / 20.0f);
        if (db >= speech_db_ - kEndBelowSpeechDb) {
            speech_ms_ += ms;
            quiet_ms_ = 0;
            return false;
        }
        quiet_ms_ += ms;
        if (speech_ms_ >= kMinSpeechMs && quiet_ms_ >= kEndSilenceMs) {
            ended_ = true;
            return true;
        }
        return false;
    }

    bool speech_started() const { return speech_ms_ > 0; }

private:
    float noise_db_ = 0;
    bool have_noise_ = false;
    float speech_db_ = -120;
    uint32_t speech_ms_ = 0;
    uint32_t quiet_ms_ = 0;
    bool ended_ = false;
};

// "Stop", "Abbrechen", "Vergiss es", ... — the whole utterance is a request
// to stop listening (e.g. after a false wake word), not a command for HA.
bool is_cancel_phrase(const char* text);

} // namespace app::voice

#endif // APP_CPP_VOICE_SPEECHENDPOINTER_H
