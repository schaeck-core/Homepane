#include "PairingController.h"

#include "../net/WebSocketConnection.h"

#include <cjson/cJSON.h>

#include <utility>

namespace app::application {

using app::core::LogLevel;
using app::home_assistant::HaClient;
using app::home_assistant::HaClientState;
using app::home_assistant::ResultMessage;
using app::net::WebSocketConnection;

PairingController::PairingController(app::core::IMonotonicClock& clock, app::core::ILogger* logger,
                                      std::unique_ptr<app::net::IWebSocketTransport> transport)
    : clock_(clock), logger_(logger), pending_transport_(std::move(transport)) {}

void PairingController::exchange_long_lived_token(const std::string& ws_url, const std::string& access_token,
                                                   const std::string& client_name,
                                                   std::function<void(std::string)> on_success,
                                                   std::function<void(std::string)> on_error) {
    on_success_ = std::move(on_success);
    on_error_ = std::move(on_error);
    client_name_ = client_name;

    auto connection = std::make_unique<WebSocketConnection>("pairing", std::move(pending_transport_), clock_);
    client_ = std::make_unique<HaClient>(std::move(connection), clock_, access_token, logger_);
    client_->set_on_state_change([this](HaClientState state) { handle_state_change(state); });
    deadline_ms_ = clock_.now_ms() + kTimeoutMs;
    if (logger_) logger_->log(LogLevel::Info, ("PairingController: connecting to " + ws_url).c_str());
    client_->start(ws_url);
}

void PairingController::handle_state_change(HaClientState state) {
    switch (state) {
        case HaClientState::Ready:
            if (logger_) logger_->log(LogLevel::Info, "PairingController: HaClient Ready, requesting long-lived token");
            send_long_lived_token_request();
            break;
        case HaClientState::AuthFailed:
            fail_once("auth_invalid");
            break;
        case HaClientState::BackoffWait:
            // One-shot flow: a failed connect/disconnect is a failure, not
            // something to silently retry forever with a short-lived token
            // (that previously left the device's loading overlay up
            // indefinitely). The connection itself is stopped in poll().
            fail_once("connection to Home Assistant failed");
            break;
        case HaClientState::Connecting:
        case HaClientState::TransportConnected:
        case HaClientState::Authenticating:
        case HaClientState::Stopped:
            break;
    }
}

void PairingController::send_long_lived_token_request() {
    cJSON* cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "type", "auth/long_lived_access_token");
    cJSON_AddStringToObject(cmd, "client_name", client_name_.c_str());
    cJSON_AddNumberToObject(cmd, "lifespan", 3650);
    char* json = cJSON_PrintUnformatted(cmd);
    std::string command = json ? json : "";
    cJSON_free(json);
    cJSON_Delete(cmd);

    client_->send_command(command, [this](const ResultMessage& result) {
        if (!result.success) {
            fail_once(result.error_message.empty() ? "auth/long_lived_access_token failed" : result.error_message);
            return;
        }
        cJSON* token_json = cJSON_Parse(result.result_json.c_str());
        if (!cJSON_IsString(token_json) || !token_json->valuestring) {
            cJSON_Delete(token_json);
            fail_once("malformed auth/long_lived_access_token result");
            return;
        }
        std::string token = token_json->valuestring;
        cJSON_Delete(token_json);
        succeed_once(std::move(token));
    });
}

void PairingController::succeed_once(std::string token) {
    if (completed_) return;
    completed_ = true;
    if (on_success_) on_success_(std::move(token));
}

void PairingController::fail_once(const std::string& message) {
    if (completed_) return;
    completed_ = true;
    if (logger_) logger_->log(LogLevel::Error, ("PairingController: " + message).c_str());
    if (on_error_) on_error_(message);
}

void PairingController::poll(uint64_t now_ms) {
    if (!client_ || stopped_) return;

    if (!completed_ && now_ms >= deadline_ms_) fail_once("timed out");

    // Not done from inside the completion callbacks themselves (they run
    // nested inside HaClient/WebSocketConnection dispatch): close the
    // one-shot connection on the next poll once the outcome is known —
    // before polling the client, so a due backoff reconnect never fires.
    if (completed_) {
        stopped_ = true;
        client_->stop();
        return;
    }

    client_->poll(now_ms);
}

} // namespace app::application
