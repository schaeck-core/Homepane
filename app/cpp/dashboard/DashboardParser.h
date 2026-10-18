#ifndef APP_CPP_DASHBOARD_DASHBOARDPARSER_H
#define APP_CPP_DASHBOARD_DASHBOARDPARSER_H

#include "DashboardModel.h"

#include <string>

namespace app::dashboard {

// Turns the raw "result" object of a `lovelace/config` response (i.e.
// HaClient's ResultMessage::result_json — the caller has already
// stripped the outer {"id":...,"type":"result",...} envelope) into an
// owned DashboardModel. Never touches LVGL (ARCHITECTURE.md §7 Phase 6 DoD).
class DashboardParser {
public:
    struct ParseError {
        std::string json_path;
        std::string message;
    };

    // Returns true and fills out_model on success. Returns false and
    // fills out_error only for structural failures (invalid JSON, no
    // "views" array) — individual malformed cards are parsed leniently
    // with best-effort/default fields rather than failing the whole
    // dashboard, matching "unbekannte optionale Felder sind erlaubt".
    static bool parse(const std::string& lovelace_result_json, DashboardModel& out_model, ParseError& out_error);
};

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_DASHBOARDPARSER_H
