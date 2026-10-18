#ifndef APP_CPP_APPLICATION_PANELLISTPARSER_H
#define APP_CPP_APPLICATION_PANELLISTPARSER_H

#include <string>
#include <vector>

namespace app::application {

// One selectable dashboard, as shown in the Setup consumer's dropdown
// (CPP_MIGRATION_PLAN.md Phase 10's "Setup/Token/Dashboard/Music-
// Consumer" split).
struct PanelOption {
    std::string title;    // shown to the user
    std::string url_path; // stored as haas_dashboard, later passed to
                           // app_cpp_dashboard_render()'s dashboard_url_path
};

// Turns the raw "result" object of a `get_panels` response (i.e.
// HaClient's ResultMessage::result_json — the caller has already
// stripped the outer {"id":...,"type":"result",...} envelope) into the
// list of Lovelace dashboards available to pick from. Never touches
// LVGL.
//
// HA's get_panels result is an object keyed by panel id (component
// name or url_path — never relied on here), each value shaped like:
//   {"component_name": "lovelace", "config": {"mode": "storage", ...},
//    "title": "Home", "url_path": "lovelace-home", ...}
// Only entries whose "config" object has a "mode" key are real Lovelace
// dashboards — HA's built-in panels (e.g. "config", "developer-tools")
// have their own "config" object too, just without "mode", and are
// silently skipped, matching the legacy app/views/setup.c's own filter
// exactly.
class PanelListParser {
public:
    static std::vector<PanelOption> parse(const std::string& get_panels_result_json);
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_PANELLISTPARSER_H
