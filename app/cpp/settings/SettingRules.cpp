#include "SettingRules.h"

namespace app::settings {

const std::vector<Dependency>& panel_dependencies()
{
    static const std::vector<Dependency> deps = {
        // The voice assistant listens through the microphones; the
        // microphones alone also serve other features (intercom).
        {"voice_enabled", "microphone_enabled"},
    };
    return deps;
}

std::vector<std::string> SettingRules::set(const std::string& key, bool on, const Get& get, const Set& set) const
{
    std::vector<std::string> changed;
    if (on) enable(key, get, set, changed);
    else disable(key, get, set, changed);
    // The key itself is what the user changed, not a side effect.
    std::vector<std::string> side_effects;
    for (const auto& k : changed) {
        if (k != key) side_effects.push_back(k);
    }
    return side_effects;
}

void SettingRules::enable(const std::string& key, const Get& get, const Set& set,
                          std::vector<std::string>& changed) const
{
    if (!get(key)) {
        set(key, true);
        changed.push_back(key);
    }
    for (const auto& d : deps_) {
        if (d.key == key && !get(d.requires_key)) enable(d.requires_key, get, set, changed);
    }
}

void SettingRules::disable(const std::string& key, const Get& get, const Set& set,
                           std::vector<std::string>& changed) const
{
    if (get(key)) {
        set(key, false);
        changed.push_back(key);
    }
    for (const auto& d : deps_) {
        if (d.requires_key == key && get(d.key)) disable(d.key, get, set, changed);
    }
}

} // namespace app::settings
