#ifndef APP_CPP_APPLICATION_DASHBOARDCONTROLLER_H
#define APP_CPP_APPLICATION_DASHBOARDCONTROLLER_H

#include "../core/DashboardGeneration.h"
#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../core/IWallClock.h"
#include "../dashboard/DashboardSession.h"
#include "../dashboard/WidgetFactory.h"
#include "../home_assistant/EntitySubscriptionCoordinator.h"
#include "../home_assistant/EntityStore.h"
#include "../home_assistant/HaClient.h"
#include "../home_assistant/HaCommandService.h"
#include "../home_assistant/QueryCoordinator.h"
#include "../net/IWebSocketTransport.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app::application {

// Live composition root for the dashboard — the "gebündelter Cutover"
// this migration has been building toward since Phase 1. Owns the one
// real HA WebSocket connection and everything hanging off it (HaClient,
// EntityStore, EntitySubscriptionCoordinator, QueryCoordinator,
// HaCommandService, WidgetFactory), and the currently-live
// DashboardSession.
//
// Platform-agnostic: the concrete IWebSocketTransport is injected by the
// caller. app_facade.cpp is the one place that #ifdef ESP_PLATFORM's
// between LwsTransport/EspWebSocketTransport, matching the existing
// pattern in PortableDefaults.cpp — this class never sees that choice.
//
// Deliberately does NOT touch Setup/Token/Music (the plan's own
// "Setup/Token/Dashboard/Music-Consumer" split, confirmed against the
// live code: app/views/setup.c and app/apis/music_assistant_api.c /
// app/widgets/music_player.c are separate consumers of the legacy
// websocket_client.c API, untouched by this class). Only the Dashboard
// consumer (app/views/dashboard.c's card-rendering path) is cut over.
class DashboardController {
public:
    DashboardController(app::core::IMonotonicClock& clock, app::core::IWallClock& wall_clock,
                         app::core::ILogger* logger, std::unique_ptr<app::net::IWebSocketTransport> transport);

    // Connects (on the first call) and, once Ready, fetches the given
    // dashboard's lovelace config and builds the live DashboardSession
    // under `parent`. Safe to call more than once with a different
    // parent/dashboard_url_path (e.g. a future "switch dashboard"
    // action): tears down any previous session first via the same
    // ordered shutdown() every dashboard switch already uses.
    //
    // on_ready fires once after the session's widgets are attached
    // in-tree. on_error fires on auth failure or a failed lovelace/config
    // fetch/parse — but only ever before the first successful render, to
    // match the legacy callback_ws_error() contract: a transient
    // reconnect blip on an already-showing dashboard must not blow it
    // away or re-trigger the error overlay (widgets already show their
    // own `stale` state for that — docs/ARCHITECTURE.md §4/§6).
    void render(lv_obj_t* parent, const std::string& ws_url, const std::string& access_token,
                const std::string& dashboard_url_path, std::function<void()> on_ready,
                std::function<void()> on_error);

    void poll(uint64_t now_ms);

    // Fetches the shown dashboard's config again and rebuilds it in place
    // (same parent, same view). Also done by itself when HA reports the
    // dashboard changed (event lovelace_updated, sent when it is saved in
    // the dashboard editor). A no-op until connected.
    void reload();

    // "Token" consumer support (C++ migration phase 10's
    // Setup/Token/Dashboard/Music-Consumer split): the settings screen's
    // "Reset Connection" button needs to stop the *live* HA connection
    // this class owns, not the legacy websocket_client.c one dashboard.c
    // stopped using at cutover. Matches HaClient::stop()'s own contract
    // (a resettable Stopped state — the caller must call render() again,
    // e.g. after the user updates their token; this class does not
    // attempt a live in-place reconnect, matching the settings screen's
    // own "please restart the application" messaging). A no-op if no
    // connection has been started yet.
    void disconnect();

    // The live HA connection (voice assistant shares it); null before the
    // first render().
    app::home_assistant::HaClient* ha_client() { return client_.get(); }
    // For app-level features beyond the dashboard (doorbell, screensaver):
    // the same entity subscriptions and service calls the tiles use. Null
    // until the first render().
    app::home_assistant::EntitySubscriptionCoordinator* coordinator() { return coordinator_.get(); }
    app::home_assistant::HaCommandService* commands() { return commands_.get(); }
    // The built dashboard (null before the first build / after disconnect).
    app::dashboard::DashboardSession* session() { return session_.get(); }
    // Tears the built dashboard down (UI thread), e.g. before its parent's
    // objects are deleted for a rebuild; the HA connection stays.
    void release_ui() {
        if (!session_) return;
        session_->shutdown();
        session_.reset();
    }

private:
    void handle_state_change(app::home_assistant::HaClientState state);
    void fetch_and_build_dashboard();
    void subscribe_dashboard_changes();
    void on_ha_event(const app::home_assistant::DecodedMessage& msg);

    app::core::IMonotonicClock& clock_;
    app::core::IWallClock& wall_clock_;
    app::core::ILogger* logger_;

    std::unique_ptr<app::net::IWebSocketTransport> pending_transport_; // consumed on the first render()

    lv_obj_t* parent_ = nullptr;
    std::string dashboard_url_path_;
    std::function<void()> on_ready_;
    std::function<void()> on_error_;
    bool dashboard_built_ = false;
    uint32_t lovelace_subscription_ = 0; // id of the lovelace_updated subscription, 0: none

    std::unique_ptr<app::home_assistant::HaClient> client_;
    app::home_assistant::EntityStore store_;
    std::unique_ptr<app::home_assistant::EntitySubscriptionCoordinator> coordinator_;
    std::unique_ptr<app::home_assistant::QueryCoordinator> query_coordinator_;
    std::unique_ptr<app::home_assistant::HaCommandService> commands_;
    app::dashboard::WidgetFactory factory_;

    app::core::DashboardGeneration current_generation_;
    std::unique_ptr<app::dashboard::DashboardSession> session_;
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_DASHBOARDCONTROLLER_H
