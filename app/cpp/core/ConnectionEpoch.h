#ifndef APP_CPP_CORE_CONNECTIONEPOCH_H
#define APP_CPP_CORE_CONNECTIONEPOCH_H

#include <cstdint>

namespace app::core {

// Strong type: increments once per (re)connect attempt of a single named
// connection. Commands/events tag the epoch they were produced under so
// stale results from a superseded connection attempt can be dropped at
// drain time instead of acting on outdated state (docs/ARCHITECTURE.md §6).
class ConnectionEpoch {
public:
    constexpr ConnectionEpoch() = default;
    constexpr explicit ConnectionEpoch(uint32_t value) : value_(value) {}

    constexpr uint32_t value() const { return value_; }

    constexpr ConnectionEpoch next() const { return ConnectionEpoch(value_ + 1); }

    constexpr bool operator==(const ConnectionEpoch& other) const { return value_ == other.value_; }
    constexpr bool operator!=(const ConnectionEpoch& other) const { return !(*this == other); }

private:
    uint32_t value_ = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_CONNECTIONEPOCH_H
