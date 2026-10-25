#include "DashboardController.h"

#include "../dashboard/DashboardParser.h"
#include "../dashboard/DefaultWidgetRegistration.h"
#include "../net/WebSocketConnection.h"

#include <cjson/cJSON.h>

#include <utility>

namespace app::application {

using app::core::LogLevel;
using app::dashboard::DashboardModel;
using app::dashboard::DashboardParser;
using app::dashboard::DashboardSession;
using app::dashboard::register_default_widgets;
using app::home_assistant::DecodedMessage;
using app::home_assistant::DecodedMessageKind;
using app::home_assistant::HaClient;
using app::home_assistant::HaClientState;
using app::home_assistant::HaCommandService;
using app::home_assistant::HaInstanceId;
using app::home_assistant::QueryCoordinator;
using app::home_assistant::ResultMessage;
using app::net::WebSocketConnection;

namespace {
// The default dashboard is listed by get_panels as "lovelace", but
// lovelace/config only accepts it as url_path null ("Unknown config
// specified: lovelace" otherwise).
std::string build_lovelace_config_request(const std::string& dashboard_url_path) {
    if (dashboard_url_path.empty() || dashboard_url_path == "lovelace") {
        return "{\"type\":\"lovelace/config\",\"force\":true,\"url_path\":null}";
    }
    return "{\"type\":\"lovelace/config\",\"force\":true,\"url_path\":\"" + dashboard_url_path + "\"}";
}

// HA's default dashboard is "lovelace" in get_panels and null in events.
bool same_dashboard(const std::string& a, const std::string& b) {
    auto norm = [](const std::string& s) { return s.empty() ? std::string("lovelace") : s; };
    return norm(a) == norm(b);
}
} // namespace

DashboardController::DashboardController(app::core::IMonotonicClock& clock, app::core::IWallClock& wall_clock,
                                          app::core::ILogger* logger,
                                          std::unique_ptr<app::net::IWebSocketTransport> transport)
    : clock_(clock), wall_clock_(wall_clock), logger_(logger), pending_transport_(std::move(transport)) {}

void DashboardController::render(lv_obj_t* parent, const std::string& ws_url, const std::string& access_token,
                                  const std::string& dashboard_url_path, std::function<void()> on_ready,
                                  std::function<void()> on_error) {
    parent_ = parent;
    dashboard_url_path_ = dashboard_url_path;
    on_ready_ = std::move(on_ready);
    on_error_ = std::move(on_error);

    if (client_) {
        // Already connecting/connected from an earlier render() call — if
        // already Ready, the dashboard for the new parent/path can be
        // fetched immediately; otherwise the in-flight connect's
        // handle_state_change() will pick it up once Ready.
        dashboard_built_ = false;
        if (client_->state() == HaClientState::Ready) fetch_and_build_dashboard();
        return;
    }

    // Registry lists (devices, entities) of a real home exceed 64 KB; they are
    // fetched rarely and parsed into PSRAM.
    auto connection = std::make_unique<WebSocketConnection>("homeassistant", std::move(pending_transport_), clock_,
                                                            1024 * 1024, 32, app::net::BackoffPolicy(1000, 30000), logger_);
    client_ = std::make_unique<HaClient>(std::move(connection), clock_, access_token, logger_);
    coordinator_ = std::make_unique<app::home_assistant::EntitySubscriptionCoordinator>(
        store_, *client_, HaInstanceId{"homeassistant"}, clock_);
    query_coordinator_ = std::make_unique<QueryCoordinator>(*client_, clock_, wall_clock_);
    commands_ = std::make_unique<HaCommandService>(*client_);
    register_default_widgets(factory_, HaInstanceId{"homeassistant"}, *commands_);

    client_->set_on_state_change([this](HaClientState state) { handle_state_change(state); });
    client_->add_event_listener([this](const DecodedMessage& msg) { on_ha_event(msg); });
    if (logger_) logger_->log(LogLevel::Info, ("DashboardController: connecting to " + ws_url).c_str());
    client_->start(ws_url);
}

void DashboardController::handle_state_change(HaClientState state) {
    switch (state) {
        case HaClientState::Ready:
            if (logger_) logger_->log(LogLevel::Info, "DashboardController: HaClient Ready");
            coordinator_->on_ha_ready();
            query_coordinator_->on_ha_ready();
            if (!dashboard_built_) fetch_and_build_dashboard();
            subscribe_dashboard_changes();
            break;
        case HaClientState::AuthFailed:
            // Only report failure before the dashboard has ever loaded —
            // once real widgets are showing, an auth failure means the
            // token was revoked mid-session, which is not this class's
            // job to surface (matches "on_error ... only ever before the
            // first successful render" in the header contract).
            if (!dashboard_built_ && on_error_) on_error_();
            break;
        case HaClientState::Connecting:
        case HaClientState::TransportConnected:
        case HaClientState::Authenticating:
            break;
        case HaClientState::BackoffWait:
        case HaClientState::Stopped:
            lovelace_subscription_ = 0; // subscriptions end with the connection
            coordinator_->on_ha_disconnected();
            query_coordinator_->on_ha_disconnected();
            break;
    }
}

void DashboardController::fetch_and_build_dashboard() {
    std::string cmd = build_lovelace_config_request(dashboard_url_path_);
    client_->send_command(cmd, [this](const ResultMessage& result) {
        if (!result.success) {
            if (!dashboard_built_ && on_error_) on_error_();
            return;
        }

        DashboardModel model;
        DashboardParser::ParseError parse_error;
        if (!DashboardParser::parse(result.result_json, model, parse_error)) {
            if (logger_) logger_->log(LogLevel::Error, parse_error.message.c_str());
            if (!dashboard_built_ && on_error_) on_error_();
            return;
        }

        // A reload keeps the view on screen.
        const int view = session_ ? session_->current_view() : 0;
        // ARCHITECTURE.md §3 Shutdown-Reihenfolge, same as every other dashboard switch.
        if (session_) {
            session_->shutdown();
            session_.reset();
        }
        current_generation_ = current_generation_.next();
        session_ = std::make_unique<DashboardSession>(std::move(model), factory_, current_generation_,
                                                        coordinator_.get(), query_coordinator_.get(), logger_);
        session_->create(parent_);
        if (view > 0 && view < session_->view_count()) session_->show_view(view, false);
        dashboard_built_ = true;
        if (logger_) {
            std::string msg =
                "DashboardController: dashboard built, " + std::to_string(session_->widget_count()) + " widgets";
            logger_->log(LogLevel::Info, msg.c_str());
        }
        if (on_ready_) on_ready_();
    });
}

void DashboardController::reload() {
    if (client_ && client_->state() == HaClientState::Ready && dashboard_built_) fetch_and_build_dashboard();
}

void DashboardController::subscribe_dashboard_changes() {
    uint32_t id = 0;
    client_->send_command(R"({"type":"subscribe_events","event_type":"lovelace_updated"})",
                          [](const ResultMessage&) {}, 10000, &id);
    lovelace_subscription_ = id; // events carry the subscribing command's id
}

void DashboardController::on_ha_event(const DecodedMessage& msg) {
    if (msg.kind != DecodedMessageKind::RawEvent || lovelace_subscription_ == 0 ||
        msg.event_subscription_id != static_cast<int>(lovelace_subscription_)) {
        return;
    }
    // {"event_type":"lovelace_updated","data":{"url_path":"dashboard-x","mode":"storage"},...}
    cJSON* root = cJSON_Parse(msg.raw_event_json.c_str());
    const cJSON* data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : nullptr;
    const cJSON* path = data ? cJSON_GetObjectItemCaseSensitive(data, "url_path") : nullptr;
    const std::string changed = cJSON_IsString(path) ? path->valuestring : "";
    cJSON_Delete(root);
    if (!same_dashboard(changed, dashboard_url_path_)) return;
    const uint32_t delay = change_delay_ ? change_delay_() : 0;
    if (delay == 0) {
        if (logger_) logger_->log(LogLevel::Info, "DashboardController: dashboard changed in HA, reloading");
        reload();
        return;
    }
    // every further change starts the wait again
    reload_at_ = clock_.now_ms() + delay;
    if (logger_) logger_->log(LogLevel::Info, "DashboardController: dashboard changed in HA, reloading once editing ends");
}

void DashboardController::poll(uint64_t now_ms) {
    if (!client_) return;
    if (reload_at_ && now_ms >= reload_at_) {
        reload_at_ = 0;
        reload();
    }
    client_->poll(now_ms);
    coordinator_->poll(now_ms);
    query_coordinator_->poll(now_ms);
}

void DashboardController::disconnect() {
    if (!client_) return;
    if (logger_) logger_->log(LogLevel::Info, "DashboardController: disconnect() requested");
    client_->stop();
}

} // namespace app::application
