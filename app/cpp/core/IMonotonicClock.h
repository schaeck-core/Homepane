#ifndef APP_CPP_CORE_IMONOTONICCLOCK_H
#define APP_CPP_CORE_IMONOTONICCLOCK_H

#include <cstdint>

namespace app::core {

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    virtual uint64_t now_ms() const = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_IMONOTONICCLOCK_H
