// Host unit test for C++ migration phase 1.
// Deliberately has no LVGL and no network dependency: it only links
// app/cpp/core (header-only) + app/cpp/application/Application.cpp,
// so it can run on the build host without a display or HA connection.

#include "../../app/cpp/application/Application.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

class RecordingLogger final : public app::core::ILogger {
public:
    void log(app::core::LogLevel, const char* message) override {
        messages.emplace_back(message);
    }
    std::vector<std::string> messages;
};

class FakeClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override { return 42; }
};

class FakeMemoryMetrics final : public app::core::IMemoryMetrics {
public:
    size_t free_heap_bytes() const override { return 1234; }
};

class NoopTaskRunner final : public app::core::ITaskRunner {
public:
    void post(void (*fn)(void*), void* ctx, const app::core::PostOptions& = {}) override {
        if (fn) fn(ctx);
    }
};

int g_failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description);
    }
}

bool contains(const std::vector<std::string>& haystack, const char* needle) {
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

int main() {
    RecordingLogger logger;
    FakeClock clock;
    FakeMemoryMetrics memoryMetrics;
    NoopTaskRunner taskRunner;

    app::application::Application app(logger, clock, taskRunner, memoryMetrics);

    expect(logger.messages.empty(), "no log output before start()");

    app.start();
    expect(contains(logger.messages, "Application::start"), "start() logs a start message");

    app.poll(); // must not crash; Phase 1 poll() is intentionally a no-op

    app.stop();
    expect(contains(logger.messages, "Application::stop"), "stop() logs a stop message");

    size_t count_after_first_stop = logger.messages.size();
    app.stop(); // idempotency: second stop() must not log again
    expect(logger.messages.size() == count_after_first_stop, "stop() is idempotent");

    if (g_failures == 0) {
        std::printf("All Application smoke tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
