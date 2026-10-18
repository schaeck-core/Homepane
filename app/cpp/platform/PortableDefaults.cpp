#include "PortableDefaults.h"

#include <chrono>
#include <cstdio>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_heap_caps.h"
#else
#include <cstdio>
#endif

namespace app::platform {

using app::core::LogLevel;

void PortableLogger::log(LogLevel level, const char* message) {
#ifdef ESP_PLATFORM
    static constexpr const char* TAG = "app_cpp";
    switch (level) {
        case LogLevel::Debug: ESP_LOGD(TAG, "%s", message); break;
        case LogLevel::Info:  ESP_LOGI(TAG, "%s", message); break;
        case LogLevel::Warn:  ESP_LOGW(TAG, "%s", message); break;
        case LogLevel::Error: ESP_LOGE(TAG, "%s", message); break;
    }
#else
    const char* prefix = "INFO";
    switch (level) {
        case LogLevel::Debug: prefix = "DEBUG"; break;
        case LogLevel::Info:  prefix = "INFO";  break;
        case LogLevel::Warn:  prefix = "WARN";  break;
        case LogLevel::Error: prefix = "ERROR"; break;
    }
    std::fprintf(stderr, "[app_cpp][%s] %s\n", prefix, message);
#endif
}

uint64_t PortableMonotonicClock::now_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

double PortableWallClock::now_epoch_s() const {
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

size_t PortableMemoryMetrics::free_heap_bytes() const {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
#else
    return 0; // not meaningfully available on desktop
#endif
}

void SynchronousTaskRunner::post(void (*fn)(void* ctx), void* ctx, const app::core::PostOptions&) {
    if (fn) fn(ctx);
}

} // namespace app::platform
