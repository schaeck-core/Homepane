#ifndef APP_CPP_PLATFORM_PORTABLEDEFAULTS_H
#define APP_CPP_PLATFORM_PORTABLEDEFAULTS_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../core/IMemoryMetrics.h"
#include "../core/ITaskRunner.h"
#include "../core/IWallClock.h"

// Placeholder platform-interface implementations for Phase 1 only.
// They exist to prove the C++ skeleton links and runs on both
// Simulator and ESP32-P4. Real per-platform adapters (Simulator:
// libwebsockets/pthread based; ESP32-P4: FreeRTOS/esp_timer based)
// land in C++ migration phases 2/11 and replace this
// file outright rather than extend it.
namespace app::platform {

class PortableLogger final : public app::core::ILogger {
public:
    void log(app::core::LogLevel level, const char* message) override;
};

class PortableMonotonicClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override;
};

class PortableWallClock final : public app::core::IWallClock {
public:
    double now_epoch_s() const override;
};

class PortableMemoryMetrics final : public app::core::IMemoryMetrics {
public:
    size_t free_heap_bytes() const override;
};

// Synchronous stand-in: runs the task immediately on the calling
// thread, ignoring PostOptions entirely. Only used where a caller
// needs *an* ITaskRunner but not the real bounded queue (e.g. the
// host unit tests). The real Application uses UiDispatcher (Phase 2).
class SynchronousTaskRunner final : public app::core::ITaskRunner {
public:
    void post(void (*fn)(void* ctx), void* ctx, const app::core::PostOptions& options = {}) override;
};

} // namespace app::platform

#endif // APP_CPP_PLATFORM_PORTABLEDEFAULTS_H
