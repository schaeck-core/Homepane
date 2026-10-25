#ifndef APP_CPP_HARDWARE_CAPABILITIES_H
#define APP_CPP_HARDWARE_CAPABILITIES_H

#include <cstring>
#include <string>

namespace app::hardware {

// What the panel's extra hardware offers, found by probing at boot
// (docs/plans/HARDWARE_CAPABILITIES.md). Platform code fills it, app/ only
// reads it: settings grey out what is missing, the rest of the UI and MQTT
// leave it out. A capability is only set when the part answered and a
// driver for it exists. Fixed for the rest of the boot once `detected`.
struct Capabilities {
    bool detected = false; // probing finished; before that everything counts as missing

    // BOS1921 + piezo actuators
    bool haptics = false;
    bool force = false; // press force and press gestures
    // SHTC3
    bool temperature = false;
    bool humidity = false;
    // ambient light sensor (later)
    bool illuminance = false;
    // mmWave: LD2410 has presence and distance, LD6004 also targets and positions
    bool presence = false;
    bool target_count = false;
    bool target_position = false;

    std::string parts; // detected chips for system info, e.g. "SHTC3, LD2410"; "" = none

    bool operator==(const Capabilities& o) const
    {
        return detected == o.detected && haptics == o.haptics && force == o.force && temperature == o.temperature &&
               humidity == o.humidity && illuminance == o.illuminance && presence == o.presence &&
               target_count == o.target_count && target_position == o.target_position && parts == o.parts;
    }
    bool operator!=(const Capabilities& o) const { return !(*this == o); }
};

// The simulator's HOMEPANE_HW: "none" or a comma list of "no-haptics",
// "no-climate", "no-presence", "ld2410" (presence without targets), applied
// to `all`. Unknown words are ignored.
inline Capabilities apply_override(Capabilities all, const char* spec)
{
    if (!spec) return all;
    std::string s(spec);
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        const std::string word = s.substr(start, end - start);
        if (word == "none") {
            Capabilities none;
            none.detected = all.detected;
            all = none;
        } else if (word == "no-haptics") {
            all.haptics = all.force = false;
        } else if (word == "no-climate") {
            all.temperature = all.humidity = false;
        } else if (word == "no-presence") {
            all.presence = all.target_count = all.target_position = false;
        } else if (word == "ld2410") {
            all.target_count = all.target_position = false;
        }
        start = end + 1;
    }
    return all;
}

} // namespace app::hardware

#endif // APP_CPP_HARDWARE_CAPABILITIES_H
