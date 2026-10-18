#include "HaClient.h"

#include "HaProtocolDecoder.h"

#include <cjson/cJSON.h>

#include <memory>
#include <utility>

namespace app::home_assistant {

using app::core::CancellationSource;
using app::core::ConnectionEpoch;
using app::core::LogLevel;
using app::net::ConnectionState;
using app::net::ReceivedMessage;
using app::net::WebSocketConnection;

namespace {
struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;
} // namespace

HaClient::HaClient(std::unique_ptr<WebSocketConnection> connection,
                    app::core::IMonotonicClock& clock,
                    std::string access_token,
                    app::core::ILogger* logger)
    : connection_(std::move(connection)),
      clock_(clock),
      access_token_(std::move(access_token)),
      logger_(logger) {
    connection_->set_on_message([this](const ReceivedMessage& msg) { on_ws_message(msg); });
    connection_->set_on_state_change(
        [this](ConnectionState s, ConnectionEpoch e) { on_ws_state_change(s, e); });
}

void HaClient::add_event_listener(EventHandler handler) { event_listeners_.push_back(std::move(handler)); }
void HaClient::set_on_state_change(StateHandler handler) { on_state_change_ = std::move(handler); }

void HaClient::set_state(HaClientState state) {
    state_ = state;
    if (on_state_change_) on_state_change_(state_);
}

void HaClient::start(const std::string& url) {
    if (state_ == HaClientState::AuthFailed) {
        // Deliberate: auth_invalid is not auto-retried. The caller must
        // start() again explicitly (docs/ARCHITECTURE.md §6).
        if (logger_) logger_->log(LogLevel::Warn, "HaClient::start() after AuthFailed — retrying explicitly");
    }
    connection_->connect(url);
}

void HaClient::stop() {
    fail_all_pending("disconnected");
    connection_->close();
    set_state(HaClientState::Stopped);
}

app::core::ConnectionEpoch HaClient::epoch() const { return connection_->epoch(); }

void HaClient::send_auth() {
    std::string auth = "{\"type\":\"auth\",\"access_token\":\"" + access_token_ + "\"}";
    connection_->send(auth);
    auth_sent_ = true;
}

bool HaClient::send_binary(std::string payload) {
    if (state_ != HaClientState::Ready) return false;
    return connection_->send_binary(std::move(payload)).request_id != 0;
}

CancellationSource HaClient::send_command(const std::string& json_without_id,
                                           ResultCallback result_cb,
                                           uint32_t timeout_ms,
                                           uint32_t* id_out) {
    CancellationSource source;
    if (id_out) *id_out = 0;

    if (state_ != HaClientState::Ready) {
        if (result_cb) {
            ResultMessage r;
            r.success = false;
            r.error_code = "not_ready";
            result_cb(r);
        }
        return source;
    }

    CJsonPtr root(cJSON_Parse(json_without_id.c_str()));
    if (!root || !cJSON_IsObject(root.get())) {
        if (result_cb) {
            ResultMessage r;
            r.success = false;
            r.error_code = "invalid_command";
            result_cb(r);
        }
        return source;
    }

    uint32_t id = next_id_++;
    cJSON_AddNumberToObject(root.get(), "id", id);
    char* text = cJSON_PrintUnformatted(root.get());
    if (!text) {
        if (result_cb) {
            ResultMessage r;
            r.success = false;
            r.error_code = "encode_failed";
            result_cb(r);
        }
        return source;
    }
    std::string serialized = text;
    cJSON_free(text);

    connection_->send(serialized);
    if (id_out) *id_out = id;

    PendingRequest pending;
    pending.callback = std::move(result_cb);
    pending.cancellation = source.token();
    pending.deadline_ms = clock_.now_ms() + timeout_ms;
    pending.epoch = connection_->epoch();
    pending_.emplace(id, std::move(pending));

    return source;
}

void HaClient::poll(uint64_t now_ms) {
    connection_->poll(now_ms);

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now_ms >= it->second.deadline_ms) {
            if (!it->second.cancellation.is_cancelled() && it->second.callback) {
                ResultMessage r;
                r.id = static_cast<int>(it->first);
                r.success = false;
                r.error_code = "timeout";
                it->second.callback(r);
            }
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void HaClient::complete_pending(const ResultMessage& result, ConnectionEpoch msg_epoch) {
    auto it = pending_.find(static_cast<uint32_t>(result.id));
    if (it == pending_.end()) return;
    if (it->second.epoch != msg_epoch) {
        // A result whose id happens to match but from a superseded
        // connection epoch — must never complete a request from the
        // current epoch (docs/ARCHITECTURE.md §6/Phase 4 DoD).
        if (logger_) logger_->log(LogLevel::Warn, "HaClient: dropped Result from a stale epoch");
        return;
    }
    if (!it->second.cancellation.is_cancelled() && it->second.callback) {
        it->second.callback(result);
    }
    pending_.erase(it);
}

void HaClient::fail_all_pending(const char* error_code) {
    for (auto& [id, pending] : pending_) {
        if (!pending.cancellation.is_cancelled() && pending.callback) {
            ResultMessage r;
            r.id = static_cast<int>(id);
            r.success = false;
            r.error_code = error_code;
            pending.callback(r);
        }
    }
    pending_.clear();
}

void HaClient::on_ws_state_change(ConnectionState s, ConnectionEpoch /*e*/) {
    switch (s) {
        case ConnectionState::Connecting:
            set_state(HaClientState::Connecting);
            break;
        case ConnectionState::Connected:
            next_id_ = 1;
            auth_sent_ = false;
            set_state(HaClientState::TransportConnected);
            // Authenticate right away instead of waiting for auth_required.
            // On ESP32, esp_websocket_client does not deliver HA's first
            // frame when it arrives together with the handshake response —
            // auth_required only surfaced once HA closed the socket after
            // its 10 s auth timeout, so every connect attempt failed. HA
            // accepts an auth message sent before it has sent
            // auth_required (the legacy C client relied on this too).
            send_auth();
            break;
        case ConnectionState::BackoffWait:
            fail_all_pending("disconnected");
            if (state_ != HaClientState::AuthFailed) set_state(HaClientState::BackoffWait);
            break;
        case ConnectionState::Stopped:
            fail_all_pending("disconnected");
            if (state_ != HaClientState::AuthFailed) set_state(HaClientState::Stopped);
            break;
    }
}

void HaClient::on_ws_message(const ReceivedMessage& msg) {
    DecodedMessage decoded = HaProtocolDecoder::decode(msg.payload);
    switch (decoded.kind) {
        case DecodedMessageKind::AuthRequired:
            set_state(HaClientState::Authenticating);
            if (!auth_sent_) send_auth(); // already sent proactively on connect
            break;
        case DecodedMessageKind::AuthOk:
            set_state(HaClientState::Ready);
            break;
        case DecodedMessageKind::AuthInvalid:
            if (logger_) {
                logger_->log(LogLevel::Error, decoded.auth_invalid_message.empty()
                                                   ? "HaClient: auth_invalid"
                                                   : decoded.auth_invalid_message.c_str());
            }
            fail_all_pending("auth_invalid");
            set_state(HaClientState::AuthFailed);
            connection_->close(); // do not let WebSocketConnection auto-retry a bad token
            break;
        case DecodedMessageKind::Result:
            complete_pending(decoded.result, msg.epoch);
            break;
        case DecodedMessageKind::EntitySnapshot:
        case DecodedMessageKind::EntityDelta:
        case DecodedMessageKind::RawEvent:
            for (const auto& listener : event_listeners_) {
                if (listener) listener(decoded);
            }
            break;
        case DecodedMessageKind::Unknown:
        case DecodedMessageKind::Malformed:
            if (logger_ && decoded.kind == DecodedMessageKind::Malformed) {
                logger_->log(LogLevel::Warn, decoded.malformed_reason.c_str());
            }
            break;
    }
}

} // namespace app::home_assistant
