#ifndef APP_CPP_SENSORS_NULLSENSORS_H
#define APP_CPP_SENSORS_NULLSENSORS_H

#include "SensorReadings.h"

namespace app::sensors {

// A panel without extra hardware: no readings, no gestures, no haptics.
class NullSensors final : public ISensorSource {
public:
    SensorReadings read(uint64_t) override { return {}; }
    void take_gestures(const std::function<void(PressGesture)>&) override {}
    bool simulated() const override { return false; }
    app::hardware::Capabilities capabilities() const override
    {
        app::hardware::Capabilities c;
        c.detected = true;
        return c;
    }
};

} // namespace app::sensors

#endif // APP_CPP_SENSORS_NULLSENSORS_H
