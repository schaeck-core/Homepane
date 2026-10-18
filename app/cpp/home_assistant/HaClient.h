#ifndef APP_CPP_HOME_ASSISTANT_HACLIENT_H
#define APP_CPP_HOME_ASSISTANT_HACLIENT_H

#include "../core/CancellationToken.h"
#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../net/WebSocketConnection.h"
#include "HaTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app::home_assistant {

// CPP_MIGRATION_PLAN.md §7 state machine, the auth-relevant subset (the
// full diagram's transport-level states are WebSocketConnection's job,
// Phase 3):
//   Stopped -> Connecting -> TransportConnected -> Authenticating -> Ready
//   any -> BackoffWait (transient disconnect, will auto-retry)
//   any -> AuthFailed (auth_invalid: NOT a retryable network error, §7.3 —
//          the underlying connection is closed and will not reconnect on
//          its own; the caller must call start() again, e.g. after the
//          user updates their token)
enum class HaClientState {
    Stopped,
    Connecting,
    TransportConnected,
    Authenticating,
    Ready,
    BackoffWait,
    AuthFailed,
};

// Sits on top of WebSocketConnection (Phase 3): drives the HA auth
// handshake, and correlates outgoing commands with their "result" by an
// HA-protocol-level id (distinct from WebSocketConnection's own internal
// SendHandle bookkeeping). Owns no LVGL, no widget references — see
// CPP_MIGRATION_PLAN.md §3.1 HaClient.
class HaClient {
public:
    // EntitySnapshot/EntityDelta/RawEvent only — never Result (that's
    // send_command's ResultCallback) or the auth/lifecycle kinds.
    using EventHandler = std::function<void(const DecodedMessage&)>;
    using StateHandler = std::function<void(HaClientState)>;
    using ResultCallback = std::function<void(const ResultMessage&)>;

    HaClient(std::unique_ptr<app::net::WebSocketConnection> connection,
             app::core::IMonotonicClock& clock,
             std::string access_token,
             app::core::ILogger* logger = nullptr);

    // Every registered listener sees every EntitySnapshot/EntityDelta/
    // RawEvent message (CPP_MIGRATION_PLAN.md Phase 9: EntitySubscriptionCoordinator
    // and QueryCoordinator both need their own feed from the same
    // connection). Each listener filters for the kinds it cares about.
    // No removal — every current caller lives as long as HaClient itself
    // (composition-root-owned), matching the existing single-listener
    // lifetime contract.
    void add_event_listener(EventHandler handler);
    void set_on_state_change(StateHandler handler);

    void start(const std::string& url);
    void stop();

    // `json_without_id` must be a JSON object without an "id" field —
    // HaClient injects one. result_cb fires exactly once: on a matching
    // Result, on timeout, or on disconnect (CPP_MIGRATION_PLAN.md §7.4).
    // If not yet Ready, fails immediately with error_code "not_ready"
    // rather than queuing something HA would reject anyway. The returned
    // CancellationSource lets the caller cancel before any of those fire.
    // `id_out` (optional) receives the id HaClient assigned, 0 if nothing
    // was sent — needed by commands whose events can arrive before their
    // result (assist_pipeline/run).
    app::core::CancellationSource send_command(const std::string& json_without_id,
                                                ResultCallback result_cb,
                                                uint32_t timeout_ms = 10000,
                                                uint32_t* id_out = nullptr);

    // Raw binary frame (Assist pipeline audio: handler-id byte + PCM).
    // Returns false if not Ready or the send queue refused it.
    bool send_binary(std::string payload);

    void poll(uint64_t now_ms);

    HaClientState state() const { return state_; }
    app::core::ConnectionEpoch epoch() const;

private:
    struct PendingRequest {
        ResultCallback callback;
        app::core::CancellationToken cancellation;
        uint64_t deadline_ms;
        app::core::ConnectionEpoch epoch;
    };

    void on_ws_message(const app::net::ReceivedMessage& msg);
    void on_ws_state_change(app::net::ConnectionState state, app::core::ConnectionEpoch epoch);
    void send_auth();
    void set_state(HaClientState state);
    void complete_pending(const ResultMessage& result, app::core::ConnectionEpoch msg_epoch);
    void fail_all_pending(const char* error_code);

    std::unique_ptr<app::net::WebSocketConnection> connection_;
    app::core::IMonotonicClock& clock_;
    std::string access_token_;
    app::core::ILogger* logger_;

    HaClientState state_ = HaClientState::Stopped;
    uint32_t next_id_ = 1;
    // Auth already sent on this connection (proactively on connect), so a
    // late auth_required must not trigger a second auth message.
    bool auth_sent_ = false;
    std::map<uint32_t, PendingRequest> pending_;

    std::vector<EventHandler> event_listeners_;
    StateHandler on_state_change_;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_HACLIENT_H
