#ifndef APP_CPP_VOICE_SPEECHAGC_H
#define APP_CPP_VOICE_SPEECHAGC_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace app::voice {

// Microphone conditioning before audio goes to speech recognition:
// - 80 Hz high-pass (rumble, DC);
// - automatic gain towards a fixed speech peak level. The panel's
//   microphones deliver speech around -40 dBFS at arm's length even at the
//   ES7210's maximum analog gain; Whisper prefers normal levels.
//   Gain only rises during speech close to the loudest speech so far (not
//   during pauses or for quieter background voices like a TV, which would
//   pump them up) and falls once input stays loud for two blocks — a single
//   click (tapping the panel) is left to the limiter instead of ducking
//   the speech that follows;
// - soft limiter instead of hard clipping.
// Not thread-safe; one instance per capture stream, fed in order.
class SpeechAgc {
public:
    static constexpr float kTargetPeak = 16000.0f; // about -6 dBFS
    static constexpr float kMaxGain = 24.0f;       // +27.6 dB
    static constexpr float kStartGain = 8.0f;      // +18 dB: first syllables already audible
    static constexpr float kInitialNoiseRms = 20.0f; // ~-64 dBFS, the panel's quiet-room floor

    explicit SpeechAgc(float sample_rate = 16000.0f)
        : hp_a_(std::exp(-2.0f * 3.14159265f * 80.0f / sample_rate))
    {
    }

    void reset()
    {
        gain_ = kStartGain;
        peak_env_ = 0;
        noise_rms_ = kInitialNoiseRms;
        loud_blocks_ = 0;
        speech_rms_ = 0;
        prev_rms_ = 0;
        hp_x_ = hp_y_ = 0;
    }

    // In place; blocks of any length (gain decisions per <= 64 ms piece).
    void process(int16_t* samples, size_t count)
    {
        while (count) {
            const size_t n = std::min(count, kBuf);
            process_block(samples, n);
            samples += n;
            count -= n;
        }
    }

    float gain() const { return gain_; }

private:
    static constexpr size_t kBuf = 1024;

    void process_block(int16_t* samples, size_t count)
    {
        float peak = 0;
        double sq = 0;
        for (size_t i = 0; i < count; ++i) {
            const float x = samples[i];
            hp_y_ = hp_a_ * (hp_y_ + x - hp_x_);
            hp_x_ = x;
            buf_[i] = hp_y_;
            peak = std::max(peak, std::fabs(hp_y_));
            sq += static_cast<double>(hp_y_) * hp_y_;
        }
        const float rms = static_cast<float>(std::sqrt(sq / count));

        // Noise floor: follows quiet blocks down at once, creeps up slowly.
        // Starts from the measured room floor rather than the first block,
        // which may be a click or already speech.
        if (rms < noise_rms_) noise_rms_ = rms;
        else noise_rms_ *= 1.002f;
        // Near-field speech: well above the noise floor and within 12 dB of
        // the loudest speech seen (which decays ~1 dB/s).
        // Only levels held for two blocks count (a click must not set it).
        speech_rms_ = std::max(std::min(rms, prev_rms_), speech_rms_ * 0.9977f);
        prev_rms_ = rms;
        const bool speech = rms > std::max(noise_rms_ * 4.0f, 30.0f) && rms > speech_rms_ * 0.25f;

        const float block_desired = std::clamp(kTargetPeak / std::max(peak, 1.0f), 1.0f, kMaxGain);
        loud_blocks_ = block_desired < gain_ ? loud_blocks_ + 1 : 0;
        if (loud_blocks_ >= 2) {
            // Sustained loud input: back off to what the louder block needs.
            peak_env_ = std::max(peak, peak_env_);
        } else {
            // A short loud block never raises the envelope (not even as the
            // very first block); it only decays here.
            peak_env_ *= 0.995f;
        }
        const float desired = std::clamp(kTargetPeak / std::max(peak_env_, 1.0f), 1.0f, kMaxGain);
        if (desired < gain_ && loud_blocks_ >= 2) gain_ = desired;
        else if (speech && desired > gain_) gain_ = std::min(desired, gain_ * 1.01f); // ~+4 dB/s while talking

        for (size_t i = 0; i < count; ++i) {
            samples[i] = static_cast<int16_t>(limit(buf_[i] * gain_));
        }
    }

    // Soft knee above 24000, approaching but never reaching full scale.
    static float limit(float y)
    {
        const float knee = 24000.0f, range = 32767.0f - knee;
        const float a = std::fabs(y);
        if (a <= knee) return y;
        const float over = (a - knee) / range;
        const float out = knee + range * (over / (1.0f + over));
        return y < 0 ? -out : out;
    }

    float hp_a_;
    float hp_x_ = 0, hp_y_ = 0;
    float gain_ = kStartGain;
    float peak_env_ = 0;
    float noise_rms_ = kInitialNoiseRms;
    int loud_blocks_ = 0;
    float speech_rms_ = 0;
    float prev_rms_ = 0;
    float buf_[kBuf] = {};
};

} // namespace app::voice

#endif // APP_CPP_VOICE_SPEECHAGC_H
