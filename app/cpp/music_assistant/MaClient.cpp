#include "MaClient.h"

#include "MaProtocolDecoder.h"

#include <cjson/cJSON.h>

#include <memory>
#include <utility>

namespace app::music_assistant {

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

// auth/login's SuccessResultMessage.result carries the new session token
// as "access_token" (confirmed against a real server — the earlier
// speculative "token" field name in docs/ARCHITECTURE.md §14 was wrong).
std::string extract_access_token(const std::string& result_json) {
    CJsonPtr parsed(cJSON_Parse(result_json.c_str()));
    if (!parsed) return {};
    cJSON* token = cJSON_GetObjectItemCaseSensitive(parsed.get(), "access_token");
    if (token && cJSON_IsString(token) && token->valuestring && token->valuestring[0] != '\0') {
        return token->valuestring;
    }
    return {};
}
} // namespace

MaClient::MaClient(std::unique_ptr<WebSocketConnection> connection, app::core::IMonotonicClock& clock,
                    MaCredentials credentials, std::string cached_token, app::core::ILogger* logger)
    : connection_(std::move(connection)),
      clock_(clock),
      credentials_(std::move(credentials)),
      cached_token_(std::move(cached_token)),
      logger_(logger) {
    connection_->set_on_message([this](const ReceivedMessage& msg) { on_ws_message(msg); });
    connection_->set_on_state_change([this](ConnectionState s, ConnectionEpoch e) { on_ws_state_change(s, e); });
}

void MaClient::set_on_state_change(StateHandler handler) { on_state_change_ = std::move(handler); }
void MaClient::set_on_token_issued(TokenIssuedHandler handler) { on_token_issued_ = std::move(handler); }
void MaClient::set_on_event(EventHandler handler) { on_event_ = std::move(handler); }

void MaClient::set_state(MaClientState state) {
    state_ = state;
    if (on_state_change_) on_state_change_(state_);
}

void MaClient::start(const std::string& url) {
    if (state_ == MaClientState::AuthFailed) {
        // Deliberate: a rejected password is not auto-retried. The caller
        // must start() again explicitly (mirrors HaClient's auth_invalid
        // contract) — presumably after the user has updated the stored
        // credentials.
        if (logger_) logger_->log(LogLevel::Warn, "MaClient::start() after AuthFailed — retrying explicitly");
    }
    connection_->connect(url);
}

void MaClient::stop() {
    fail_all_pending("disconnected");
    connection_->close();
    set_state(MaClientState::Stopped);
}

CancellationSource MaClient::send_command(const std::string& command, const std::string& args_json,
                                           ResultCallback result_cb, uint32_t timeout_ms) {
    if (state_ != MaClientState::Ready) {
        CancellationSource source;
        if (result_cb) {
            MaResultMessage r;
            r.success = false;
            r.error_message = "not_ready";
            result_cb(r);
        }
        return source;
    }
    return send_internal(command, args_json, std::move(result_cb), timeout_ms);
}

CancellationSource MaClient::send_internal(const std::string& command, const std::string& args_json,
                                            ResultCallback result_cb, uint32_t timeout_ms) {
    CancellationSource source;

    CJsonPtr args(args_json.empty() ? cJSON_CreateObject() : cJSON_Parse(args_json.c_str()));
    if (!args || !cJSON_IsObject(args.get())) {
        if (result_cb) {
            MaResultMessage r;
            r.success = false;
            r.error_message = "invalid_args";
            result_cb(r);
        }
        return source;
    }

    CJsonPtr root(cJSON_CreateObject());
    std::string id = std::to_string(next_id_++);
    cJSON_AddStringToObject(root.get(), "message_id", id.c_str());
    cJSON_AddStringToObject(root.get(), "command", command.c_str());
    cJSON_AddItemToObject(root.get(), "args", args.release());

    char* text = cJSON_PrintUnformatted(root.get());
    if (!text) {
        if (result_cb) {
            MaResultMessage r;
            r.success = false;
            r.error_message = "encode_failed";
            result_cb(r);
        }
        return source;
    }
    std::string serialized = text;
    cJSON_free(text);

    connection_->send(serialized);

    PendingRequest pending;
    pending.callback = std::move(result_cb);
    pending.cancellation = source.token();
    pending.deadline_ms = clock_.now_ms() + timeout_ms;
    pending.epoch = connection_->epoch();
    pending_.emplace(std::move(id), std::move(pending));

    return source;
}

void MaClient::handle_server_info(const MaServerInfo& info) {
    if (info.schema_version < kMaAuthRequiredSchemaVersion) {
        if (logger_) logger_->log(LogLevel::Info, "MaClient: server predates schema 28, no auth required");
        set_state(MaClientState::Ready);
        return;
    }
    set_state(MaClientState::Authenticating);
    if (!cached_token_.empty()) {
        send_auth(cached_token_, /*fallback_to_login_on_failure=*/true);
    } else {
        send_login_with_credentials();
    }
}

void MaClient::send_auth(const std::string& token, bool fallback_to_login_on_failure) {
    CJsonPtr args(cJSON_CreateObject());
    cJSON_AddStringToObject(args.get(), "token", token.c_str());
    char* text = cJSON_PrintUnformatted(args.get());
    std::string args_json = text ? text : "{}";
    if (text) cJSON_free(text);

    send_internal("auth", args_json, [this, token, fallback_to_login_on_failure](const MaResultMessage& result) {
        if (result.success) {
            // Only *this* — a successful "auth" command — actually
            // authenticates the connection. A successful "auth/login" on
            // its own does not (confirmed against a real server: a
            // command sent immediately after a successful auth/login was
            // rejected with "Authentication is required").
            cached_token_ = token;
            set_state(MaClientState::Ready);
            return;
        }
        if (fallback_to_login_on_failure) {
            // A cached token can simply be stale/expired/revoked (MA
            // sessions are a sliding window, not truly long-lived) — not
            // itself a reason to give up; silently fall back to a fresh
            // username/password login, exactly like a first-ever
            // connection.
            if (logger_) logger_->log(LogLevel::Warn, "MaClient: cached token rejected, falling back to login");
            cached_token_.clear();
            send_login_with_credentials();
        } else {
            // A token we JUST obtained via login was rejected — retrying
            // login again would risk an infinite loop, so this is a real,
            // surfaced AuthFailed.
            if (logger_) logger_->log(LogLevel::Error, "MaClient: freshly issued token was rejected by auth");
            fail_all_pending("auth_failed");
            set_state(MaClientState::AuthFailed);
            connection_->close();
        }
    });
}

void MaClient::send_login_with_credentials() {
    CJsonPtr args(cJSON_CreateObject());
    cJSON_AddStringToObject(args.get(), "username", credentials_.username.c_str());
    cJSON_AddStringToObject(args.get(), "password", credentials_.password.c_str());
    cJSON_AddStringToObject(args.get(), "provider_id", "builtin");
    cJSON_AddStringToObject(args.get(), "device_name", credentials_.device_name.c_str());
    char* text = cJSON_PrintUnformatted(args.get());
    std::string args_json = text ? text : "{}";
    if (text) cJSON_free(text);

    send_internal("auth/login", args_json, [this](const MaResultMessage& result) {
        if (!result.success) {
            if (logger_) logger_->log(LogLevel::Error, "MaClient: auth/login rejected (bad username/password)");
            fail_all_pending("auth_failed");
            set_state(MaClientState::AuthFailed);
            connection_->close(); // do not let WebSocketConnection auto-retry bad credentials
            return;
        }
        std::string access_token = extract_access_token(result.result_json);
        if (access_token.empty()) {
            if (logger_) logger_->log(LogLevel::Error, "MaClient: auth/login succeeded but returned no access_token");
            fail_all_pending("auth_failed");
            set_state(MaClientState::AuthFailed);
            connection_->close();
            return;
        }
        if (on_token_issued_) on_token_issued_(access_token);
        // auth/login only issues a token; it does not itself authenticate
        // the connection — must still send an explicit "auth" with it.
        send_auth(access_token, /*fallback_to_login_on_failure=*/false);
    });
}

void MaClient::poll(uint64_t now_ms) {
    connection_->poll(now_ms);

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now_ms >= it->second.deadline_ms) {
            if (!it->second.cancellation.is_cancelled() && it->second.callback) {
                MaResultMessage r;
                r.message_id = it->first;
                r.success = false;
                r.error_message = "timeout";
                it->second.callback(r);
            }
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void MaClient::complete_pending(const MaResultMessage& result, ConnectionEpoch msg_epoch) {
    auto it = pending_.find(result.message_id);
    if (it == pending_.end()) return;
    if (it->second.epoch != msg_epoch) {
        // A result whose message_id happens to match but from a
        // superseded connection epoch — must never complete a request
        // from the current epoch (mirrors HaClient's own epoch guard).
        if (logger_) logger_->log(LogLevel::Warn, "MaClient: dropped Result from a stale epoch");
        return;
    }
    // Erase before invoking: the auth callbacks (send_auth_with_token/
    // send_login_with_credentials) call fail_all_pending() on failure —
    // if this entry were still in pending_ during that reentrant call, it
    // would be found and re-invoked, which re-enters fail_all_pending
    // again, unboundedly (verified via a real stack-overflow crash while
    // testing the auth/login-failure path).
    ResultCallback callback = std::move(it->second.callback);
    app::core::CancellationToken cancellation = it->second.cancellation;
    pending_.erase(it);
    if (!cancellation.is_cancelled() && callback) {
        callback(result);
    }
}

void MaClient::fail_all_pending(const std::string& error_message) {
    // Snapshot-then-clear before invoking, for the same reentrancy reason
    // as complete_pending() above: a callback may itself call
    // send_command() (repopulating pending_) or fail_all_pending() again.
    std::map<std::string, PendingRequest> pending = std::move(pending_);
    pending_.clear();
    for (auto& [id, req] : pending) {
        if (!req.cancellation.is_cancelled() && req.callback) {
            MaResultMessage r;
            r.message_id = id;
            r.success = false;
            r.error_message = error_message;
            req.callback(r);
        }
    }
}

void MaClient::on_ws_state_change(ConnectionState s, ConnectionEpoch /*e*/) {
    switch (s) {
        case ConnectionState::Connecting:
            set_state(MaClientState::Connecting);
            break;
        case ConnectionState::Connected:
            next_id_ = 1;
            // Wait for the server's unsolicited ServerInfo before doing
            // anything else — unlike HA, MA doesn't tell us to send auth,
            // it just tells us who it is and we decide from schema_version.
            set_state(MaClientState::TransportConnected);
            break;
        case ConnectionState::BackoffWait:
            fail_all_pending("disconnected");
            if (state_ != MaClientState::AuthFailed) set_state(MaClientState::BackoffWait);
            break;
        case ConnectionState::Stopped:
            fail_all_pending("disconnected");
            if (state_ != MaClientState::AuthFailed) set_state(MaClientState::Stopped);
            break;
    }
}

void MaClient::on_ws_message(const ReceivedMessage& msg) {
    MaDecodedMessage decoded = MaProtocolDecoder::decode(msg.payload);
    switch (decoded.kind) {
        case MaDecodedMessageKind::ServerInfo:
            handle_server_info(decoded.server_info);
            break;
        case MaDecodedMessageKind::Result:
            complete_pending(decoded.result, msg.epoch);
            break;
        case MaDecodedMessageKind::Event:
            // MA pushes every event to an authenticated connection (no
            // subscribe command): player/queue updates for the player UI.
            if (on_event_ && state_ == MaClientState::Ready) {
                on_event_(decoded.event_type, decoded.event_object_id, decoded.event_data_json);
            }
            break;
        case MaDecodedMessageKind::Unknown:
        case MaDecodedMessageKind::Malformed:
            if (logger_ && decoded.kind == MaDecodedMessageKind::Malformed) {
                logger_->log(LogLevel::Warn, decoded.malformed_reason.c_str());
            }
            break;
    }
}

} // namespace app::music_assistant
