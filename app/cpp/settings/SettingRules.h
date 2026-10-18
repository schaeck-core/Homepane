#ifndef APP_CPP_SETTINGS_SETTINGRULES_H
#define APP_CPP_SETTINGS_SETTINGRULES_H

#include <functional>
#include <string>
#include <vector>

namespace app::settings {

// "Setting A needs setting B": both are on/off config keys ("1"/"0").
struct Dependency {
    std::string key;
    std::string requires_key;
};

// The dependencies between on/off settings, applied whenever one changes:
// switching a setting on also switches on what it needs (transitively);
// switching one off also switches off what needs it. The caller shows the
// returned side effects to the user ("Microphone was enabled too").
class SettingRules {
public:
    using Get = std::function<bool(const std::string& key)>;
    using Set = std::function<void(const std::string& key, bool on)>;

    explicit SettingRules(std::vector<Dependency> dependencies) : deps_(std::move(dependencies)) {}

    // Sets `key` and returns the other keys that changed as a consequence.
    std::vector<std::string> set(const std::string& key, bool on, const Get& get, const Set& set) const;

private:
    void enable(const std::string& key, const Get& get, const Set& set, std::vector<std::string>& changed) const;
    void disable(const std::string& key, const Get& get, const Set& set, std::vector<std::string>& changed) const;

    std::vector<Dependency> deps_;
};

// The panel's rules (keys as in config.ini).
const std::vector<Dependency>& panel_dependencies();

} // namespace app::settings

#endif // APP_CPP_SETTINGS_SETTINGRULES_H
