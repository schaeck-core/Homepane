// Unit tests for PanelListParser (C++ migration phase 10's
// "Setup" consumer). No LVGL, no network.

#include "app/cpp/application/PanelListParser.h"

#include <cstdio>
#include <string>

using namespace app::application;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

// Shaped like a real Home Assistant get_panels response's "result"
// object: keyed by panel id, mixing real Lovelace dashboards
// ("config.mode" present) with built-in panels that have their own
// "config" object but no "mode" key, plus one panel with no "config" at
// all (matches HA's own "developer-tools" panel shape).
constexpr const char* kRealisticResult = R"({
    "lovelace": {
        "component_name": "lovelace",
        "title": "Home",
        "config": {"mode": "storage"},
        "url_path": "lovelace",
        "icon": null,
        "require_admin": false
    },
    "lovelace-test": {
        "component_name": "lovelace",
        "title": "Wohnzimmer",
        "config": {"mode": "storage"},
        "url_path": "dashboard-test",
        "icon": "mdi:sofa",
        "require_admin": false
    },
    "config": {
        "component_name": "config",
        "title": "Settings",
        "config": {},
        "url_path": "config",
        "icon": "hass:cog",
        "require_admin": true
    },
    "developer-tools": {
        "component_name": "developer-tools",
        "title": null,
        "url_path": "developer-tools",
        "icon": "hass:hammer",
        "require_admin": true
    }
})";

void test_extracts_only_real_lovelace_dashboards_in_order() {
    std::vector<PanelOption> options = PanelListParser::parse(kRealisticResult);

    expect(options.size() == 2, "built-in config/developer-tools panels are filtered out, only 2 remain");
    if (options.size() == 2) {
        expect(options[0].title == "Home" && options[0].url_path == "lovelace",
               "the first real dashboard's title/url_path are extracted correctly");
        expect(options[1].title == "Wohnzimmer" && options[1].url_path == "dashboard-test",
               "the second real dashboard's title/url_path are extracted correctly, order preserved");
    }
}

void test_malformed_json_returns_empty_not_crash() {
    std::vector<PanelOption> options = PanelListParser::parse("{not valid json");
    expect(options.empty(), "malformed JSON yields an empty list rather than crashing");
}

void test_empty_object_returns_empty() {
    std::vector<PanelOption> options = PanelListParser::parse("{}");
    expect(options.empty(), "a result with no panels at all yields an empty list");
}

void test_dashboard_missing_title_or_url_path_is_skipped() {
    constexpr const char* kMissingFields = R"({
        "lovelace-1": {"config": {"mode": "storage"}, "url_path": "no-title"},
        "lovelace-2": {"config": {"mode": "storage"}, "title": "No URL"}
    })";
    std::vector<PanelOption> options = PanelListParser::parse(kMissingFields);
    expect(options.empty(), "a dashboard missing either title or url_path is skipped, not half-added");
}

void test_default_dashboard_without_title_is_kept() {
    // real HA: the default dashboard has "title": null
    constexpr const char* kDefault = R"({
        "lovelace": {"component_name": "lovelace", "config": {"mode": "storage"}, "title": null, "url_path": "lovelace"}
    })";
    std::vector<PanelOption> options = PanelListParser::parse(kDefault);
    expect(options.size() == 1 && options[0].title.empty() && options[0].url_path == "lovelace",
           "the stored default dashboard is kept with an empty title (the UI names it)");

    // auto-generated default: no config at all -> nothing the panel can load
    constexpr const char* kAuto = R"({
        "lovelace": {"component_name": "lovelace", "config": null, "title": null, "url_path": "lovelace"}
    })";
    expect(PanelListParser::parse(kAuto).empty(), "an auto-generated default dashboard (no config) is left out");
}

} // namespace

int main() {
    test_extracts_only_real_lovelace_dashboards_in_order();
    test_malformed_json_returns_empty_not_crash();
    test_empty_object_returns_empty();
    test_dashboard_missing_title_or_url_path_is_skipped();
    test_default_dashboard_without_title_is_kept();

    if (g_failures == 0) {
        std::printf("All PanelListParser tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
