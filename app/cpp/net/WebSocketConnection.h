#ifndef APP_CPP_NET_WEBSOCKETCONNECTION_H
#define APP_CPP_NET_WEBSOCKETCONNECTION_H

#include "../core/ConnectionEpoch.h"
#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "BackoffPolicy.h"
#include "IWebSocketTransport.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace app::net {

enum class ConnectionState {
    Stopped,       // never connected, or close() was called
    Connecting,    // connect() called, transport not yet open
    Connected,     // transport open, ready to send
    BackoffWait,   // disconnected, waiting before the next reconnect attempt
};

struct ReceivedMessage {
    std::string payload; // owned, fully reassembled
    app::core::ConnectionEpoch epoch;
};

struct SendHandle {
    uint32_t request_id = 0;
    app::core::ConnectionEpoch epoch;
};

struct ConnectionTelemetry {
    uint64_t total_sent = 0;
    uint64_t total_received = 0;
    uint64_t total_send_dropped_overflow = 0;
    uint64_t total_reassembly_overflow = 0;
    uint64_t reconnect_attempts = 0;
    size_t current_queue_depth = 0;
};

// Connection-level state machine on top of IWebSocketTransport: frame
// reassembly, bounded send queue, per-epoch request ids, reconnect with
// backoff. Knows nothing about HA/MA message semantics — HaClient (Phase 4)
// is layered on top of this. docs/ARCHITECTURE.md §2/§6/§9
class WebSocketConnection final : public IWebSocketTransportSink {
public:
    using MessageHandler = std::function<void(const ReceivedMessage&)>;
    using StateHandler = std::function<void(ConnectionState, app::core::ConnectionEpoch)>;

    WebSocketConnection(std::string name,
                         std::unique_ptr<IWebSocketTransport> transport,
                         app::core::IMonotonicClock& clock,
                         size_t max_message_bytes = 64 * 1024,
                         size_t max_queue_depth = 32,
                         BackoffPolicy backoff = BackoffPolicy(1000, 30000),
                         app::core::ILogger* logger = nullptr);

    const std::string& name() const { return name_; }

    void set_on_message(MessageHandler handler);
    void set_on_state_change(StateHandler handler);

    // Injectable jitter source for reconnect backoff; defaults to 0 (no
    // jitter) so tests are deterministic. Real adapters can wire a random
    // source here.
    void set_jitter_source(std::function<double()> jitter_source);

    void connect(const std::string& url);
    void close();

    // Enqueues a message for sending; returns a handle whose epoch can be
    // compared against epoch() later to detect that a reconnect happened
    // in between (docs/ARCHITECTURE.md: request ids are connection-
    // local and reset on reconnect).
    SendHandle send(std::string payload);
    // Binary frame through the same ordered queue as send().
    SendHandle send_binary(std::string payload);

    // Must be called periodically (e.g. from Application::poll()) so
    // scheduled reconnects can fire; takes the current monotonic time so
    // it stays testable without real timers/threads.
    void poll(uint64_t now_ms);

    app::core::ConnectionEpoch epoch() const { return epoch_; }
    ConnectionState state() const { return state_; }
    ConnectionTelemetry telemetry() const { return telemetry_; }

    // IWebSocketTransportSink
    void on_transport_open() override;
    void on_transport_fragment(const uint8_t* data, size_t len, bool is_final) override;
    void on_transport_error(const char* reason) override;
    void on_transport_closed(const char* reason) override;
    void on_transport_writable() override;

private:
    void flush_send_queue();
    void reset_for_new_epoch();
    void handle_disconnect(const char* reason);
    void set_state(ConnectionState state);

    std::string name_;
    std::string url_;
    std::unique_ptr<IWebSocketTransport> transport_;
    app::core::IMonotonicClock& clock_;
    size_t max_message_bytes_;
    bool dropping_oversized_ = false;
    size_t max_queue_depth_;
    BackoffPolicy backoff_;
    app::core::ILogger* logger_;
    std::function<double()> jitter_source_ = [] { return 0.0; };

    ConnectionState state_ = ConnectionState::Stopped;
    app::core::ConnectionEpoch epoch_{};
    bool stopped_explicitly_ = true;

    std::string reassembly_buffer_;

    struct Outgoing {
        std::string data;
        bool binary = false;
    };
    SendHandle enqueue(std::string payload, bool binary);
    std::deque<Outgoing> send_queue_;
    bool writable_ = false;

    uint32_t next_request_id_ = 1;
    uint32_t reconnect_attempt_ = 0;
    uint64_t reconnect_at_ms_ = 0;

    MessageHandler on_message_;
    StateHandler on_state_change_;

    ConnectionTelemetry telemetry_;
};

} // namespace app::net

#endif // APP_CPP_NET_WEBSOCKETCONNECTION_H
