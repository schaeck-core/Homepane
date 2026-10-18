#ifndef APP_CPP_NET_BACKOFFPOLICY_H
#define APP_CPP_NET_BACKOFFPOLICY_H

#include <algorithm>
#include <cstdint>

namespace app::net {

// Exponential backoff with a deterministic, injectable jitter source so
// tests stay reproducible (docs/ARCHITECTURE.md §6: "Exponentieller
// Backoff mit Jitter und Obergrenze").
class BackoffPolicy {
public:
    BackoffPolicy(uint32_t base_ms, uint32_t max_ms) : base_ms_(base_ms), max_ms_(max_ms) {}

    // `jitter_fraction` in [0,1) scales up to +25% of the computed delay;
    // pass a deterministic value (e.g. 0) in tests, a real RNG output in
    // production adapters.
    uint32_t delay_for_attempt(uint32_t attempt, double jitter_fraction) const {
        double jitter = std::min(std::max(jitter_fraction, 0.0), 1.0);
        // attempt 0 -> base_ms_, doubling each attempt, capped at max_ms_.
        uint64_t exp = base_ms_;
        for (uint32_t i = 0; i < attempt && exp < max_ms_; ++i) exp *= 2;
        uint64_t capped = std::min<uint64_t>(exp, max_ms_);
        uint64_t with_jitter = capped + static_cast<uint64_t>(capped * 0.25 * jitter);
        return static_cast<uint32_t>(std::min<uint64_t>(with_jitter, max_ms_ + max_ms_ / 4));
    }

private:
    uint32_t base_ms_;
    uint32_t max_ms_;
};

} // namespace app::net

#endif // APP_CPP_NET_BACKOFFPOLICY_H
