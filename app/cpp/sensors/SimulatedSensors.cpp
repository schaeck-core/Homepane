#include "SimulatedSensors.h"

#include <cmath>

namespace app::sensors {

namespace {
constexpr float kPi = 3.14159265f;

uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float unit(uint32_t h) { return (h & 0xFFFFFF) / float(0xFFFFFF); } // 0..1

// Presence cycle: 6 min periods; occupied for a random 2-5 min of each.
constexpr uint64_t kCycleMs = 6 * 60 * 1000;
} // namespace

// Smooth value noise in -1..1 (interpolated random points every 20 s).
float SimulatedSensors::noise(uint64_t t_ms, uint32_t channel) const
{
    const uint64_t step = 20000;
    const uint64_t i = t_ms / step;
    const float f = float(t_ms % step) / step;
    const float a = unit(hash(seed_ * 131 + channel * 7919 + uint32_t(i))) * 2 - 1;
    const float b = unit(hash(seed_ * 131 + channel * 7919 + uint32_t(i + 1))) * 2 - 1;
    const float s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
}

SensorReadings SimulatedSensors::read(uint64_t now_ms)
{
    SensorReadings r;
    const uint64_t cycle = now_ms / kCycleMs;
    const uint64_t in_cycle = now_ms % kCycleMs;
    const uint64_t occupied_ms = 120000 + uint64_t(unit(hash(seed_ + uint32_t(cycle))) * 180000);
    const bool present = in_cycle < occupied_ms;
    r.presence = present;

    if (present) {
        const int count = 1 + int(unit(hash(seed_ * 3 + uint32_t(cycle))) * 2.99f); // 1..3
        r.target_count = count;
        // nearest target wanders in front of the panel: 0.6-3 m away
        const float t = in_cycle / 1000.0f;
        const float y = 1.6f + 1.0f * std::sin(t * 2 * kPi / 47.0f) + 0.3f * noise(now_ms, 1);
        const float x = 0.8f * std::sin(t * 2 * kPi / 31.0f) + 0.2f * noise(now_ms, 2);
        const float z = 0.15f * noise(now_ms, 3); // head height relative to the sensor
        r.target_x_m = x;
        r.target_y_m = std::fmax(0.4f, y);
        r.target_z_m = z;
        r.distance_m = std::sqrt(x * x + *r.target_y_m * *r.target_y_m + z * z);
        // speed from the analytic derivative of the path
        const float dy = 1.0f * std::cos(t * 2 * kPi / 47.0f) * 2 * kPi / 47.0f;
        const float dx = 0.8f * std::cos(t * 2 * kPi / 31.0f) * 2 * kPi / 31.0f;
        r.speed_mps = std::sqrt(dx * dx + dy * dy);
    } else {
        r.target_count = 0;
        r.speed_mps = 0.0f;
    }

    r.temperature_c = 21.5f + 0.8f * noise(now_ms / 30, 4) + (present ? 0.2f : 0.0f);
    r.humidity_pct = 45.0f + 6.0f * noise(now_ms / 30, 5);
    // daylight: 0 at night, ~400 lx at noon indoors, a lamp when dark and present
    const float day = std::sin((float(seconds_of_day_) / 86400.0f - 0.25f) * 2 * kPi);
    float lux = day > 0 ? 400.0f * day : 0.0f;
    if (present && lux < 50) lux += 120.0f;
    r.illuminance_lx = std::fmax(0.0f, lux * (1.0f + 0.05f * noise(now_ms, 6)));

    // presses: every 2-6 minutes while someone is there
    if (next_press_ms_ == 0) next_press_ms_ = now_ms + 30000;
    float force = 0;
    if (now_ms < press_until_ms_) {
        const float k = float(press_until_ms_ - now_ms) / 600.0f;
        force = 60.0f + 30.0f * std::fmin(1.0f, k);
    }
    if (present && now_ms >= next_press_ms_) {
        const bool long_press = unit(hash(seed_ * 5 + uint32_t(now_ms / 1000))) < 0.3f;
        gestures_.push_back(long_press ? PressGesture::LongPress : PressGesture::Press);
        press_until_ms_ = now_ms + (long_press ? 1200 : 300);
        next_press_ms_ = now_ms + 120000 + uint64_t(unit(hash(seed_ * 7 + uint32_t(now_ms))) * 240000);
        force = 90.0f;
    }
    r.press_force_pct = force;
    return r;
}

void SimulatedSensors::take_gestures(const std::function<void(PressGesture)>& sink)
{
    for (PressGesture g : gestures_) sink(g);
    gestures_.clear();
}

} // namespace app::sensors
