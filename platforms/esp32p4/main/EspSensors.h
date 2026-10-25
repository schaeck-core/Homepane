#ifndef PLATFORMS_ESP32P4_MAIN_ESPSENSORS_H
#define PLATFORMS_ESP32P4_MAIN_ESPSENSORS_H

#include "../../../app/cpp/sensors/SensorReadings.h"

#include <mutex>

namespace platform_esp32p4 {

// The panel's extra hardware (docs/hardware/SENSORS.md), probed once at
// boot on its own task (docs/plans/HARDWARE_CAPABILITIES.md):
// - I2C on the BSP's bus (GPIO7/8, shared with touch and the codecs):
//   SHTC3 (read), BOS1921, ENS160, SGP40 (found and listed, no driver yet);
// - UART1 on GPIO2 TX / GPIO3 RX: HLK-LD2410 (presence, distance); the
//   LD6004 follows when its protocol is known.
// The task keeps reading what it found; read() and capabilities() (UI
// thread) only copy the latest values.
class EspSensors final : public app::sensors::ISensorSource {
public:
    // Starts the probing task; call once, after the BSP's I2C bus is up.
    void start();

    app::sensors::SensorReadings read(uint64_t now_ms) override;
    void take_gestures(const std::function<void(app::sensors::PressGesture)>&) override {}
    bool simulated() const override { return false; }
    app::hardware::Capabilities capabilities() const override;

private:
    static void task(void* self);
    void run();

    mutable std::mutex mutex_;
    app::hardware::Capabilities caps_;
    app::sensors::SensorReadings readings_;
    bool started_ = false;
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPSENSORS_H
