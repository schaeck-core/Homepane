#ifndef APP_CPP_APPLICATION_SETUPCONTROLLER_H
#define APP_CPP_APPLICATION_SETUPCONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../home_assistant/HaClient.h"
#include "../net/IWebSocketTransport.h"
#include "PanelListParser.h"

#include <functional>
#include <memory>
#include <string>

namespace app::application {

// The "Setup" consumer's composition root (CPP_MIGRATION_PLAN.md Phase
// 10's "Setup/Token/Dashboard/Music-Consumer" split): connects to Home
// Assistant, fetches the list of Lovelace dashboards via `get_panels`,
// and hands the parsed list back — replacing app/views/setup.c's own
// createWSConnectionNamed()+callback_getPanels() pair. Deliberately its
// own independent connection, not a reuse of DashboardController's (Setup
// always finishes — the user picks a dashboard and continues — strictly
// before the Dashboard consumer's own connection is ever started, so
// there is no live concurrent-connection scenario to design around; see
// CPP_MIGRATION_PLAN.md's own state machine: INIT -> SETUP -> COMPLETE).
//
// Deliberately narrower than DashboardController: no EntityStore/
// QueryCoordinator/WidgetFactory (Setup never subscribes to entities),
// so no IWallClock dependency either.
class SetupController {
public:
    SetupController(app::core::IMonotonicClock& clock, app::core::ILogger* logger,
                     std::unique_ptr<app::net::IWebSocketTransport> transport);

    // Connects (on the first call) and, once Ready, sends `get_panels`
    // and parses the result. on_success fires exactly once with the
    // parsed dashboard list (possibly empty, if the server truly has
    // none — that is not itself an error). on_error fires on auth
    // failure or a failed `get_panels` request, but only ever before the
    // first successful fetch (matches DashboardController's own
    // on_error contract).
    void fetch_panels(const std::string& ws_url, const std::string& access_token,
                       std::function<void(std::vector<PanelOption>)> on_success,
                       std::function<void()> on_error);

    void poll(uint64_t now_ms);

private:
    void handle_state_change(app::home_assistant::HaClientState state);
    void send_get_panels();

    app::core::IMonotonicClock& clock_;
    app::core::ILogger* logger_;

    std::unique_ptr<app::net::IWebSocketTransport> pending_transport_; // consumed on the first fetch_panels()

    std::function<void(std::vector<PanelOption>)> on_success_;
    std::function<void()> on_error_;
    bool fetched_ = false;

    std::unique_ptr<app::home_assistant::HaClient> client_;
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_SETUPCONTROLLER_H
