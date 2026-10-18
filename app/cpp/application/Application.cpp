#include "Application.h"

#include <cstdio>

namespace app::application {

using app::core::ILogger;
using app::core::IMonotonicClock;
using app::core::ITaskRunner;
using app::core::IMemoryMetrics;
using app::core::LogLevel;

Application::Application(ILogger& logger,
                          IMonotonicClock& clock,
                          ITaskRunner& taskRunner,
                          IMemoryMetrics& memoryMetrics)
    : logger_(logger), clock_(clock), taskRunner_(taskRunner), memoryMetrics_(memoryMetrics) {}

void Application::start() {
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Application::start at %llu ms, free_heap=%zu bytes",
                  static_cast<unsigned long long>(clock_.now_ms()),
                  memoryMetrics_.free_heap_bytes());
    logger_.log(LogLevel::Info, msg);
    started_ = true;
}

void Application::poll() {
    taskRunner_.drain();
}

void Application::stop() {
    if (!started_) return;
    logger_.log(LogLevel::Info, "Application::stop");
    started_ = false;
}

} // namespace app::application
