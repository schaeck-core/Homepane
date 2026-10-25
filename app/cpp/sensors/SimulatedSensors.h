#ifndef APP_CPP_SENSORS_SIMULATEDSENSORS_H
#define APP_CPP_SENSORS_SIMULATEDSENSORS_H

#include "SensorReadings.h"

#include <vector>

namespace app::sensors {

// Plausible, slowly varying fake data for every sensor, so the MQTT/HA side
// can be built and tested before the hardware exists. Deterministic for a
// given seed and time (host tests). Behaviour:
// - presence: someone walks up, stays for a while (1-3 targets moving in
//   front of the panel), leaves; empty room in between;
// - temperature/humidity drift slowly around 21.5 C / 45 %;
// - illuminance follows the time of day (seconds since midnight given);
// - a press (sometimes a long press) every few minutes while present.
// Pretends to have the planned hardware (SHTC3, BOS1921, LD6004; no light
// sensor yet) unless set_capabilities() says otherwise; readings and
// gestures follow the capabilities.
class SimulatedSensors final : public ISensorSource {
public:
    explicit SimulatedSensors(uint32_t seed = 1) : seed_(seed) {}

    // Local time of day for the light curve; defaults to noon.
    void set_seconds_of_day(uint32_t s) { seconds_of_day_ = s; }

    static app::hardware::Capabilities planned_hardware();
    void set_capabilities(const app::hardware::Capabilities& c) { caps_ = c; }

    SensorReadings read(uint64_t now_ms) override;
    void take_gestures(const std::function<void(PressGesture)>& sink) override;
    void haptic(int effect) override
    {
        if (caps_.haptics) last_haptic_ = effect;
    }
    bool simulated() const override { return true; }
    app::hardware::Capabilities capabilities() const override { return caps_; }
    int last_haptic() const { return last_haptic_; }

private:
    float noise(uint64_t t_ms, uint32_t channel) const;

    uint32_t seed_;
    app::hardware::Capabilities caps_ = planned_hardware();
    uint32_t seconds_of_day_ = 12 * 3600;
    std::vector<PressGesture> gestures_;
    uint64_t next_press_ms_ = 0;
    uint64_t press_until_ms_ = 0;
    int last_haptic_ = 0;
};

} // namespace app::sensors

#endif // APP_CPP_SENSORS_SIMULATEDSENSORS_H
