#ifndef APP_CPP_SENSORS_SENSORREADINGS_H
#define APP_CPP_SENSORS_SENSORREADINGS_H

#include "../hardware/Capabilities.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace app::sensors {

// Everything the panel's planned extra hardware measures (see
// docs/hardware/SENSORS.md): HLK-LD2410 / LD6004 mmWave radar (UART), SHTC3
// temperature/humidity, later an ambient light sensor, BOS1921 piezo force
// sensing (I2C). Values a source does not have stay empty.
struct SensorReadings {
    // mmWave
    std::optional<bool> presence;
    std::optional<int> target_count;
    std::optional<float> target_x_m, target_y_m, target_z_m; // nearest target, panel-relative
    std::optional<float> distance_m;
    std::optional<float> speed_mps;
    // climate / light
    std::optional<float> temperature_c;
    std::optional<float> humidity_pct;
    std::optional<float> illuminance_lx;
    // piezo
    std::optional<float> press_force_pct; // 0..100, of the four actuators
};

enum class PressGesture { Press, LongPress };

// A source of sensor data: simulated in the simulator, the hardware drivers
// on the device. Polled on the UI thread; implementations do their I/O
// elsewhere.
class ISensorSource {
public:
    virtual ~ISensorSource() = default;
    virtual SensorReadings read(uint64_t now_ms) = 0;
    // Gestures detected since the last call (piezo presses).
    virtual void take_gestures(const std::function<void(PressGesture)>& sink) = 0;
    // Haptic feedback (effect number, 1 = strong click); sources without one ignore it.
    virtual void haptic(int effect) { (void)effect; }
    // What the connected hardware offers (detected = false while probing).
    virtual app::hardware::Capabilities capabilities() const = 0;
    // True for made-up data (shown in HA so nobody mistakes it for real).
    virtual bool simulated() const = 0;
};

} // namespace app::sensors

#endif // APP_CPP_SENSORS_SENSORREADINGS_H
