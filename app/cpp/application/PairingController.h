#ifndef APP_CPP_APPLICATION_PAIRINGCONTROLLER_H
#define APP_CPP_APPLICATION_PAIRINGCONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../home_assistant/HaClient.h"
#include "../net/IWebSocketTransport.h"

#include <functional>
#include <memory>
#include <string>

namespace app::application {

// The OAuth device-pairing flow's WebSocket half (CPP_MIGRATION_PLAN.md
// Phase 10's last unmigrated consumer). The platform webserver
// (platforms/simulator/webserver.c / platforms/esp32p4/main/
// webserver_esp32.c) already exchanged an HA OAuth authorization code for
// a short-lived access token over plain HTTP before ever reaching this
// class — that leg stays C, no IHttpClient abstraction exists or is
// needed here. This class authenticates to Home Assistant's websocket API
// with that short-lived token and requests a permanent long-lived access
// token via HA's `auth/long_lived_access_token` command, replacing
// app/websocket_client.c's getLongLivedToken() / platforms/esp32p4/main/
// websocket_client_esp.c's getLongLivedToken().
//
// Deliberately one-shot: a fresh instance is constructed per attempt (see
// app_facade.cpp), unlike SetupController's "reconnect the same instance"
// pattern — each attempt hands in a brand-new short-lived access token, so
// there is nothing meaningful to reuse from a prior attempt.
class PairingController {
public:
    PairingController(app::core::IMonotonicClock& clock, app::core::ILogger* logger,
                       std::unique_ptr<app::net::IWebSocketTransport> transport);

    // access_token: the short-lived OAuth access token obtained from HA's
    // /auth/token endpoint (NOT the eventual long-lived token). client_name:
    // passed verbatim as auth/long_lived_access_token's "client_name" field
    // (callers reproduce the legacy "LVGL <panel-ip>" convention). on_success
    // fires exactly once with the new long-lived token string; on_error
    // fires exactly once (auth failure, a failed/malformed result, a failed
    // connect/disconnect, or the overall timeout) — never both. The one-shot
    // connection is closed on the next poll() after either outcome. Persisting the token and any resulting
    // app-state transition (enter_setup_state()) is the caller's
    // responsibility, mirroring SetupController/DashboardController.
    void exchange_long_lived_token(const std::string& ws_url, const std::string& access_token,
                                    const std::string& client_name, std::function<void(std::string)> on_success,
                                    std::function<void(std::string)> on_error);

    void poll(uint64_t now_ms);

private:
    void handle_state_change(app::home_assistant::HaClientState state);
    void send_long_lived_token_request();
    void succeed_once(std::string token);
    void fail_once(const std::string& message);

    app::core::IMonotonicClock& clock_;
    app::core::ILogger* logger_;
    std::unique_ptr<app::net::IWebSocketTransport> pending_transport_;
    std::string client_name_;

    std::function<void(std::string)> on_success_;
    std::function<void(std::string)> on_error_;
    bool completed_ = false;
    bool stopped_ = false;

    // Upper bound for the whole exchange (connect + auth + command), so a
    // hung connection can never leave the caller waiting forever.
    static constexpr uint64_t kTimeoutMs = 20000;
    uint64_t deadline_ms_ = 0;

    std::unique_ptr<app::home_assistant::HaClient> client_;
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_PAIRINGCONTROLLER_H
