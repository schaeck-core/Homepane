#ifndef APP_CPP_MUSIC_ASSISTANT_MACLIENT_H
#define APP_CPP_MUSIC_ASSISTANT_MACLIENT_H

#include "../core/CancellationToken.h"
#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../net/WebSocketConnection.h"
#include "MaTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace app::music_assistant {

// Servers older than this never require auth at all (music-assistant.io:
// "Starting with Music Assistant Server schema version 28, authentication
// is required to connect to the server. Connections to older servers
// (schema < 28) will continue to work without authentication.").
constexpr int kMaAuthRequiredSchemaVersion = 28;

enum class MaClientState {
    Stopped,
    Connecting,
    TransportConnected,  // transport is up; waiting for the server's unsolicited ServerInfo
    Authenticating,
    Ready,
    BackoffWait,
    AuthFailed,  // a username/password login attempt was rejected — not auto-retried (mirrors HaClient's auth_invalid contract)
};

struct MaCredentials {
    std::string username;
    std::string password;
    std::string device_name = "LVGL Panel";
};

// Sits on top of WebSocketConnection (reused unchanged — already protocol-
// agnostic), analogous to HaClient but for Music Assistant's own wire
// protocol, which differs enough that HaClient itself can't be reused:
//   - message_id is a string, not a number (see MaTypes.h).
//   - Auth is an ordinary command/result round-trip (`auth` or
//     `auth/login`), not an unsolicited auth_required/auth_ok event pair.
//   - The server sends an unsolicited ServerInfo immediately on connect,
//     which is where schema_version tells us whether auth is even needed.
//
// Auth strategy (docs/ARCHITECTURE.md §14, decided 2026-09-23 after
// verifying against MA's actual current auth system): username/password
// only, self-healing. A cached session token is tried first if one was
// supplied; if the server rejects it (expired/revoked — MA tokens are a
// 30-day sliding window, not truly long-lived), this class automatically
// falls back to a fresh username/password login rather than surfacing an
// error, so no user interaction is ever required as long as the stored
// credentials remain valid. Only a rejected *credentials* attempt is a
// real, surfaced AuthFailed.
class MaClient {
public:
    using StateHandler = std::function<void(MaClientState)>;
    using ResultCallback = std::function<void(const MaResultMessage&)>;
    // Fires whenever a fresh session token is obtained (token-auth
    // confirmed valid, or a new login succeeded) so the caller can persist
    // it (e.g. into config.h) and skip sending the password next time.
    // MaClient itself never touches config — matches HaClient's existing
    // contract of taking credentials as plain constructor data.
    using TokenIssuedHandler = std::function<void(const std::string& token)>;
    // A server event ("player_updated", "queue_updated", ...): its
    // object_id (a player or queue id) and the raw JSON of its data.
    using EventHandler =
        std::function<void(const std::string& type, const std::string& object_id, const std::string& data_json)>;

    MaClient(std::unique_ptr<app::net::WebSocketConnection> connection, app::core::IMonotonicClock& clock,
             MaCredentials credentials, std::string cached_token, app::core::ILogger* logger = nullptr);

    void set_on_state_change(StateHandler handler);
    void set_on_token_issued(TokenIssuedHandler handler);
    void set_on_event(EventHandler handler);

    void start(const std::string& url);
    void stop();

    // `command` is a bare MA command string (e.g. "player_queues/play");
    // `args_json` is a JSON object (or empty for none) — MaClient injects
    // "message_id" and wraps both into the full envelope. Same one-shot-
    // callback contract as HaClient::send_command: fires exactly once, on
    // Result, timeout, or disconnect. Fails immediately with a not_ready
    // error_message if not yet Ready (auth commands bypass this guard
    // internally — see send_internal in the .cpp).
    app::core::CancellationSource send_command(const std::string& command, const std::string& args_json,
                                                ResultCallback result_cb, uint32_t timeout_ms = 10000);

    void poll(uint64_t now_ms);

    MaClientState state() const { return state_; }

private:
    struct PendingRequest {
        ResultCallback callback;
        app::core::CancellationToken cancellation;
        uint64_t deadline_ms;
        app::core::ConnectionEpoch epoch;
    };

    app::core::CancellationSource send_internal(const std::string& command, const std::string& args_json,
                                                 ResultCallback result_cb, uint32_t timeout_ms = 10000);
    void on_ws_message(const app::net::ReceivedMessage& msg);
    void on_ws_state_change(app::net::ConnectionState state, app::core::ConnectionEpoch epoch);
    void handle_server_info(const MaServerInfo& info);
    // Sends the "auth" command with `token`. On success the connection is
    // truly authenticated (unlike a successful "auth/login" alone — see
    // send_login_with_credentials). On failure: falls back to
    // send_login_with_credentials() when `fallback_to_login_on_failure` is
    // true (a stale *cached* token is not itself fatal); otherwise (a
    // token we just obtained via login was rejected) goes straight to
    // AuthFailed, to avoid an infinite login<->auth retry loop.
    void send_auth(const std::string& token, bool fallback_to_login_on_failure);
    void send_login_with_credentials();
    void set_state(MaClientState state);
    void complete_pending(const MaResultMessage& result, app::core::ConnectionEpoch msg_epoch);
    void fail_all_pending(const std::string& error_message);

    std::unique_ptr<app::net::WebSocketConnection> connection_;
    app::core::IMonotonicClock& clock_;
    MaCredentials credentials_;
    std::string cached_token_;  // "" if none yet
    app::core::ILogger* logger_;

    MaClientState state_ = MaClientState::Stopped;
    uint32_t next_id_ = 1;
    std::map<std::string, PendingRequest> pending_;

    StateHandler on_state_change_;
    TokenIssuedHandler on_token_issued_;
    EventHandler on_event_;
};

} // namespace app::music_assistant

#endif // APP_CPP_MUSIC_ASSISTANT_MACLIENT_H
