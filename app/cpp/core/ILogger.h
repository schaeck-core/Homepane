#ifndef APP_CPP_CORE_ILOGGER_H
#define APP_CPP_CORE_ILOGGER_H

namespace app::core {

enum class LogLevel { Debug, Info, Warn, Error };

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const char* message) = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_ILOGGER_H
