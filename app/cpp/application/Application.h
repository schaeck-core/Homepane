#ifndef APP_CPP_APPLICATION_APPLICATION_H
#define APP_CPP_APPLICATION_APPLICATION_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../core/IMemoryMetrics.h"
#include "../core/ITaskRunner.h"

namespace app::application {

// Composition root skeleton (C++ migration phase 1).
// Intentionally does nothing beyond proving that the platform
// interfaces wire together and the lifecycle can be driven from
// both C entry points. HA/Dashboard/UI wiring is added in later
// phases, not here.
class Application {
public:
    Application(app::core::ILogger& logger,
                app::core::IMonotonicClock& clock,
                app::core::ITaskRunner& taskRunner,
                app::core::IMemoryMetrics& memoryMetrics);

    void start();
    void poll();
    void stop();

private:
    app::core::ILogger& logger_;
    app::core::IMonotonicClock& clock_;
    app::core::ITaskRunner& taskRunner_;
    app::core::IMemoryMetrics& memoryMetrics_;
    bool started_ = false;
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_APPLICATION_H
