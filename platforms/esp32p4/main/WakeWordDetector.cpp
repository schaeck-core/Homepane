#include "WakeWordDetector.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <algorithm>
#include <cstring>

namespace platform_esp32p4 {

namespace {
const char* TAG = "wakeword";
constexpr int kFeatures = 40;
constexpr int kStepMs = 10;
constexpr int kOps = 20;
using OpResolver = tflite::MicroMutableOpResolver<kOps>;
} // namespace

struct WakeWordDetector::Impl {
    FrontendState frontend = {};
    bool frontend_ok = false;
    uint8_t* arena = nullptr;
    size_t arena_size = 0;
    const tflite::Model* model = nullptr;
    OpResolver resolver;
    tflite::MicroAllocator* allocator = nullptr;
    tflite::MicroResourceVariables* variables = nullptr;
    std::unique_ptr<tflite::MicroInterpreter> interpreter;
    TfLiteTensor* input = nullptr;
    TfLiteTensor* output = nullptr;

    ~Impl()
    {
        interpreter.reset();
        if (frontend_ok) FrontendFreeStateContents(&frontend);
        heap_caps_free(arena);
    }
};

WakeWordDetector::WakeWordDetector() : impl_(new Impl) {}
WakeWordDetector::~WakeWordDetector() = default;

bool WakeWordDetector::init(const Model& m)
{
    Impl& d = *impl_;
    cutoff_ = m.probability_cutoff;

    // The audio frontend configuration microWakeWord models are trained with
    // (the TensorFlow micro_speech defaults with a 125-7500 Hz filterbank).
    FrontendConfig fc;
    fc.window.size_ms = 30;
    fc.window.step_size_ms = kStepMs;
    fc.filterbank.num_channels = kFeatures;
    fc.filterbank.lower_band_limit = 125.0f;
    fc.filterbank.upper_band_limit = 7500.0f;
    fc.noise_reduction.smoothing_bits = 10;
    fc.noise_reduction.even_smoothing = 0.025f;
    fc.noise_reduction.odd_smoothing = 0.06f;
    fc.noise_reduction.min_signal_remaining = 0.05f;
    fc.pcan_gain_control.enable_pcan = 1;
    fc.pcan_gain_control.strength = 0.95f;
    fc.pcan_gain_control.offset = 80.0f;
    fc.pcan_gain_control.gain_bits = 21;
    fc.log_scale.enable_log = 1;
    fc.log_scale.scale_shift = 6;
    if (!FrontendPopulateState(&fc, &d.frontend, 16000)) {
        ESP_LOGE(TAG, "frontend init failed");
        return false;
    }
    d.frontend_ok = true;

    d.model = tflite::GetModel(m.tflite);
    if (d.model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model schema %lu != %d", static_cast<unsigned long>(d.model->version()), TFLITE_SCHEMA_VERSION);
        return false;
    }
    // Ops used by the microWakeWord streaming models.
    d.resolver.AddCallOnce();
    d.resolver.AddVarHandle();
    d.resolver.AddReadVariable();
    d.resolver.AddAssignVariable();
    d.resolver.AddReshape();
    d.resolver.AddStridedSlice();
    d.resolver.AddConcatenation();
    d.resolver.AddConv2D();
    d.resolver.AddDepthwiseConv2D();
    d.resolver.AddFullyConnected();
    d.resolver.AddMul();
    d.resolver.AddAdd();
    d.resolver.AddMean();
    d.resolver.AddLogistic();
    d.resolver.AddQuantize();
    d.resolver.AddAveragePool2D();
    d.resolver.AddMaxPool2D();
    d.resolver.AddPad();
    d.resolver.AddPack();
    d.resolver.AddSplitV();

    // Arena in PSRAM: internal RAM is scarce; the model is tiny.
    d.arena_size = m.tensor_arena_size + 8192;
    d.arena = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, d.arena_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!d.arena) {
        ESP_LOGE(TAG, "no memory for the tensor arena");
        return false;
    }
    d.allocator = tflite::MicroAllocator::Create(d.arena, d.arena_size);
    d.variables = tflite::MicroResourceVariables::Create(d.allocator, 20);
    d.interpreter.reset(new tflite::MicroInterpreter(d.model, d.resolver, d.allocator, d.variables));
    if (d.interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        return false;
    }
    d.input = d.interpreter->input(0);
    d.output = d.interpreter->output(0);
    if (d.input->type != kTfLiteInt8 || d.input->dims->size != 3 || d.input->dims->data[2] != kFeatures ||
        d.output->type != kTfLiteUInt8) {
        ESP_LOGE(TAG, "unexpected model input/output layout");
        return false;
    }
    stride_ = d.input->dims->data[1];
    ESP_LOGI(TAG, "%s ready: stride %d (%d ms), cutoff %.2f, arena %u used of %u", m.name, stride_,
             stride_ * kStepMs, cutoff_, static_cast<unsigned>(d.interpreter->arena_used_bytes()),
             static_cast<unsigned>(d.arena_size));
    reset();
    return true;
}

void WakeWordDetector::reset()
{
    Impl& d = *impl_;
    if (d.frontend_ok) FrontendReset(&d.frontend);
    if (d.variables) d.variables->ResetAll();
    std::memset(history_, 0, sizeof(history_));
    history_pos_ = 0;
    slices_ = 0;
    ms_since_reset_ = 0;
    refractory_ms_ = 0;
}

bool WakeWordDetector::process(const int16_t* samples, size_t count)
{
    Impl& d = *impl_;
    if (!d.interpreter) return false;
    bool detected = false;
    while (count) {
        size_t read = 0;
        FrontendOutput out = FrontendProcessSamples(&d.frontend, samples, count, &read);
        samples += read;
        count -= read;
        if (!out.values) {
            if (read == 0) break;
            continue; // needs more samples for the next 10 ms step
        }
        // uint16 log-mel energies -> int8 the way the models were trained
        // (TF micro_speech: value * 256 / 666, shifted to signed).
        int8_t features[kFeatures];
        for (int i = 0; i < kFeatures; ++i) {
            int32_t v = (static_cast<int32_t>(out.values[i]) * 256 + 333) / 666 - 128;
            features[i] = static_cast<int8_t>(std::clamp<int32_t>(v, -128, 127));
        }
        ms_since_reset_ += kStepMs;
        if (refractory_ms_) refractory_ms_ = refractory_ms_ > kStepMs ? refractory_ms_ - kStepMs : 0;
        if (feed_features(features)) detected = true;
    }
    return detected;
}

bool WakeWordDetector::feed_features(const int8_t* features)
{
    Impl& d = *impl_;
    std::memcpy(d.input->data.int8 + slices_ * kFeatures, features, kFeatures);
    if (++slices_ < stride_) return false;
    slices_ = 0;

    const int64_t t0 = esp_timer_get_time();
    if (d.interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGW(TAG, "Invoke failed");
        return false;
    }
    invoke_us_total_ += static_cast<uint64_t>(esp_timer_get_time() - t0);
    ++invokes_;

    history_[history_pos_] = d.output->data.uint8[0];
    history_pos_ = (history_pos_ + 1) % kSlidingWindow;
    uint32_t sum = 0;
    for (uint8_t p : history_) sum += p;
    last_mean_ = static_cast<float>(sum) / (kSlidingWindow * 255.0f);

    if (ms_since_reset_ < kWarmupMs || refractory_ms_ || last_mean_ < cutoff_) return false;
    refractory_ms_ = kRefractoryMs;
    std::memset(history_, 0, sizeof(history_));
    return true;
}

} // namespace platform_esp32p4
