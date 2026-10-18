#ifndef APP_CPP_CORE_IMEMORYMETRICS_H
#define APP_CPP_CORE_IMEMORYMETRICS_H

#include <cstddef>

namespace app::core {

class IMemoryMetrics {
public:
    virtual ~IMemoryMetrics() = default;
    // Returns 0 where the platform cannot report this (e.g. desktop simulator).
    virtual size_t free_heap_bytes() const = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_IMEMORYMETRICS_H
