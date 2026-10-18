#include "SetupController.h"

#include "../net/WebSocketConnection.h"

#include <utility>

namespace app::application {

using app::core::LogLevel;
using app::home_assistant::HaClient;
using app::home_assistant::HaClientState;
using app::home_assistant::ResultMessage;
using app::net::WebSocketConnection;

SetupController::SetupController(app::core::IMonotonicClock& clock, app::core::ILogger* logger,
                                  std::unique_ptr<app::net::IWebSocketTransport> transport)
    : clock_(clock), logger_(logger), pending_transport_(std::move(transport)) {}

void SetupController::fetch_panels(const std::string& ws_url, const std::string& access_token,
                                    std::function<void(std::vector<PanelOption>)> on_success,
                                    std::function<void()> on_error) {
    on_success_ = std::move(on_success);
    on_error_ = std::move(on_error);

    if (client_) {
        // Already connecting/connected from an earlier call — if already
        // Ready, fetch immediately; otherwise the in-flight connect's
        // handle_state_change() will pick it up once Ready.
        fetched_ = false;
        if (client_->state() == HaClientState::Ready) send_get_panels();
        return;
    }

    auto connection = std::make_unique<WebSocketConnection>("homeassistant", std::move(pending_transport_), clock_);
    client_ = std::make_unique<HaClient>(std::move(connection), clock_, access_token, logger_);
    client_->set_on_state_change([this](HaClientState state) { handle_state_change(state); });
    if (logger_) logger_->log(LogLevel::Info, ("SetupController: connecting to " + ws_url).c_str());
    client_->start(ws_url);
}

void SetupController::handle_state_change(HaClientState state) {
    switch (state) {
        case HaClientState::Ready:
            if (logger_) logger_->log(LogLevel::Info, "SetupController: HaClient Ready");
            if (!fetched_) send_get_panels();
            break;
        case HaClientState::AuthFailed:
            // Only before the first successful fetch — matches
            // DashboardController's own on_error contract.
            if (!fetched_ && on_error_) on_error_();
            break;
        case HaClientState::Connecting:
        case HaClientState::TransportConnected:
        case HaClientState::Authenticating:
        case HaClientState::BackoffWait:
        case HaClientState::Stopped:
            break;
    }
}

void SetupController::send_get_panels() {
    client_->send_command(R"({"type":"get_panels"})", [this](const ResultMessage& result) {
        if (!result.success) {
            if (!fetched_ && on_error_) on_error_();
            return;
        }
        fetched_ = true;
        std::vector<PanelOption> options = PanelListParser::parse(result.result_json);
        if (logger_) {
            std::string msg = "SetupController: fetched " + std::to_string(options.size()) + " dashboard(s)";
            logger_->log(LogLevel::Info, msg.c_str());
        }
        if (on_success_) on_success_(std::move(options));
    });
}

void SetupController::poll(uint64_t now_ms) {
    if (client_) client_->poll(now_ms);
}

} // namespace app::application
