#ifndef PLATFORMS_ESP32P4_MAIN_WAKEWORDDETECTOR_H
#define PLATFORMS_ESP32P4_MAIN_WAKEWORDDETECTOR_H

#include <cstddef>
#include <cstdint>
#include <memory>

namespace platform_esp32p4 {

// On-device wake word with a microWakeWord streaming model (TensorFlow Lite
// Micro; models from github.com/esphome/micro-wake-word-models, Apache 2.0,
// embedded from main/wakeword/). Voice plan V3.
//
// Pipeline: 16 kHz mono PCM -> TFLM microfrontend (30 ms window, 10 ms step,
// 40 mel channels; the configuration the models are trained with) -> int8
// features -> model invoked every `stride` feature slices -> the mean of the
// last kSlidingWindow probabilities against the model's cutoff.
//
// Not thread-safe; owned and fed by the capture task.
class WakeWordDetector {
public:
    struct Model {
        const uint8_t* tflite;
        float probability_cutoff;   // from the model's JSON manifest
        size_t tensor_arena_size;   // from the manifest; some margin is added
        const char* name;
    };

    static constexpr int kSlidingWindow = 5;
    static constexpr uint32_t kRefractoryMs = 2000; // after a detection
    static constexpr uint32_t kWarmupMs = 1000;     // streaming state settles first

    WakeWordDetector();
    ~WakeWordDetector();

    bool init(const Model& model);
    // Feeds audio; returns true when the wake word was detected in it.
    bool process(const int16_t* samples, size_t count);
    // Forget streaming state and history (e.g. after the assistant ran).
    void reset();

    // Diagnostics: last mean probability (0..1), average inference time.
    float last_probability() const { return last_mean_; }
    uint32_t avg_invoke_us() const { return invokes_ ? static_cast<uint32_t>(invoke_us_total_ / invokes_) : 0; }

private:
    struct Impl;
    bool feed_features(const int8_t* features);

    std::unique_ptr<Impl> impl_;
    float cutoff_ = 0.97f;
    uint8_t history_[kSlidingWindow] = {};
    int history_pos_ = 0;
    int stride_ = 1;
    int slices_ = 0;
    uint32_t ms_since_reset_ = 0;
    uint32_t refractory_ms_ = 0;
    float last_mean_ = 0;
    uint64_t invoke_us_total_ = 0;
    uint32_t invokes_ = 0;
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_WAKEWORDDETECTOR_H
