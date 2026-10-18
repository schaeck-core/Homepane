#ifndef APP_CPP_SENSORS_SENSORREADINGS_H
#define APP_CPP_SENSORS_SENSORREADINGS_H

#include <cstdint>
#include <functional>
#include <optional>

namespace app::sensors {

// Everything the panel's planned extra hardware measures (see
// docs/hardware/SENSORS.md): HLK-LD6004 mmWave radar (UART), SHT45
// temperature/humidity and an ambient light sensor (I2C), four piezo
// pressure sensors (one ADC envelope). Values a source does not have stay
// empty.
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
    std::optional<float> press_force_pct; // 0..100, envelope of the four sensors
};

enum class PressGesture { Press, LongPress };

// A source of sensor data: simulated now, the hardware drivers later.
// Polled on the UI thread; implementations do their I/O elsewhere.
class ISensorSource {
public:
    virtual ~ISensorSource() = default;
    virtual SensorReadings read(uint64_t now_ms) = 0;
    // Gestures detected since the last call (piezo presses).
    virtual void take_gestures(const std::function<void(PressGesture)>& sink) = 0;
    // Haptic feedback (DRV2605L effect number); sources without one ignore it.
    virtual void haptic(int effect) { (void)effect; }
    // True for made-up data (shown in HA so nobody mistakes it for real).
    virtual bool simulated() const = 0;
};

} // namespace app::sensors

#endif // APP_CPP_SENSORS_SENSORREADINGS_H
