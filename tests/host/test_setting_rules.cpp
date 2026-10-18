// Unit tests for SettingRules: dependencies between on/off settings.

#include "app/cpp/settings/SettingRules.h"

#include <cstdio>
#include <map>
#include <string>

using app::settings::Dependency;
using app::settings::SettingRules;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

struct Store {
    std::map<std::string, bool> v;
    SettingRules::Get get = [this](const std::string& k) { return v[k]; };
    SettingRules::Set set = [this](const std::string& k, bool on) { v[k] = on; };
};

} // namespace

int main()
{
    SettingRules rules(app::settings::panel_dependencies());
    {
        Store s;
        auto side = rules.set("voice_enabled", true, s.get, s.set);
        expect(s.v["voice_enabled"] && s.v["microphone_enabled"], "enabling the assistant enables the microphone");
        expect(side.size() == 1 && side[0] == "microphone_enabled", "the microphone is reported as a side effect");
        side = rules.set("microphone_enabled", false, s.get, s.set);
        expect(!s.v["microphone_enabled"] && !s.v["voice_enabled"], "disabling the microphone disables the assistant");
        expect(side.size() == 1 && side[0] == "voice_enabled", "the assistant is reported as a side effect");
    }
    {
        Store s;
        s.v["microphone_enabled"] = true;
        auto side = rules.set("voice_enabled", true, s.get, s.set);
        expect(side.empty(), "no side effect when the requirement is already on");
        side = rules.set("voice_enabled", false, s.get, s.set);
        expect(side.empty() && s.v["microphone_enabled"], "disabling the assistant keeps the microphone (intercom)");
    }
    {
        // Transitive: a -> b -> c.
        SettingRules chain({{"a", "b"}, {"b", "c"}});
        Store s;
        auto side = chain.set("a", true, s.get, s.set);
        expect(s.v["b"] && s.v["c"] && side.size() == 2, "requirements are enabled transitively");
        side = chain.set("c", false, s.get, s.set);
        expect(!s.v["a"] && !s.v["b"] && side.size() == 2, "dependents are disabled transitively");
    }
    if (g_failures == 0) {
        std::printf("All setting rule tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
