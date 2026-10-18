#ifndef APP_CPP_CORE_IWALLCLOCK_H
#define APP_CPP_CORE_IWALLCLOCK_H

namespace app::core {

// Real calendar time (UNIX epoch seconds), distinct from IMonotonicClock
// (ms since boot, no relation to wall-clock date/time — unsuitable for
// computing an HA `recorder/statistics_during_period` request's
// start_time/end_time, which are real ISO8601 timestamps). Injectable
// for the same reason IMonotonicClock is: host tests fix "now" instead
// of depending on the real system clock (C++ migration phase 9).
class IWallClock {
public:
    virtual ~IWallClock() = default;
    virtual double now_epoch_s() const = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_IWALLCLOCK_H
