#include "WebSocketConnection.h"

#include <cstdio>
#include <utility>

namespace app::net {

using app::core::ConnectionEpoch;
using app::core::LogLevel;

WebSocketConnection::WebSocketConnection(std::string name,
                                          std::unique_ptr<IWebSocketTransport> transport,
                                          app::core::IMonotonicClock& clock,
                                          size_t max_message_bytes,
                                          size_t max_queue_depth,
                                          BackoffPolicy backoff,
                                          app::core::ILogger* logger)
    : name_(std::move(name)),
      transport_(std::move(transport)),
      clock_(clock),
      max_message_bytes_(max_message_bytes),
      max_queue_depth_(max_queue_depth),
      backoff_(backoff),
      logger_(logger) {
    transport_->set_sink(this);
}

void WebSocketConnection::set_on_message(MessageHandler handler) { on_message_ = std::move(handler); }
void WebSocketConnection::set_on_state_change(StateHandler handler) { on_state_change_ = std::move(handler); }
void WebSocketConnection::set_jitter_source(std::function<double()> jitter_source) {
    jitter_source_ = std::move(jitter_source);
}

void WebSocketConnection::set_state(ConnectionState state) {
    state_ = state;
    if (on_state_change_) on_state_change_(state_, epoch_);
}

void WebSocketConnection::reset_for_new_epoch() {
    reassembly_buffer_.clear();
    dropping_oversized_ = false;
    send_queue_.clear();
    telemetry_.current_queue_depth = 0;
    writable_ = false;
    next_request_id_ = 1;
}

void WebSocketConnection::connect(const std::string& url) {
    url_ = url;
    stopped_explicitly_ = false;
    reconnect_attempt_ = 0;
    epoch_ = epoch_.next();
    reset_for_new_epoch();
    set_state(ConnectionState::Connecting);
    transport_->connect(url_);
}

void WebSocketConnection::close() {
    stopped_explicitly_ = true;
    transport_->close();
    reset_for_new_epoch();
    set_state(ConnectionState::Stopped);
}

SendHandle WebSocketConnection::send(std::string payload) { return enqueue(std::move(payload), false); }

SendHandle WebSocketConnection::send_binary(std::string payload) { return enqueue(std::move(payload), true); }

SendHandle WebSocketConnection::enqueue(std::string payload, bool binary) {
    if (send_queue_.size() >= max_queue_depth_) {
        ++telemetry_.total_send_dropped_overflow;
        if (logger_) logger_->log(LogLevel::Warn, "WebSocketConnection: send queue full, dropping message");
        return SendHandle{0, epoch_}; // request_id 0 == not queued
    }
    uint32_t id = next_request_id_++;
    send_queue_.push_back(Outgoing{std::move(payload), binary});
    telemetry_.current_queue_depth = send_queue_.size();
    if (state_ == ConnectionState::Connected && writable_) {
        flush_send_queue();
    } else {
        transport_->request_writable();
    }
    return SendHandle{id, epoch_};
}

void WebSocketConnection::flush_send_queue() {
    while (!send_queue_.empty() && writable_) {
        const Outgoing& front = send_queue_.front();
        const auto* bytes = reinterpret_cast<const uint8_t*>(front.data.data());
        bool ok = front.binary ? transport_->send_binary(bytes, front.data.size())
                               : transport_->send_text(bytes, front.data.size());
        if (!ok) {
            writable_ = false;
            transport_->request_writable();
            break;
        }
        ++telemetry_.total_sent;
        send_queue_.pop_front();
        telemetry_.current_queue_depth = send_queue_.size();
    }
}

void WebSocketConnection::poll(uint64_t now_ms) {
    transport_->dispatch_pending();
    if (state_ == ConnectionState::BackoffWait && now_ms >= reconnect_at_ms_) {
        ++telemetry_.reconnect_attempts;
        epoch_ = epoch_.next();
        reset_for_new_epoch();
        set_state(ConnectionState::Connecting);
        transport_->connect(url_);
    }
}

void WebSocketConnection::on_transport_open() {
    reconnect_attempt_ = 0;
    writable_ = true;
    set_state(ConnectionState::Connected);
    flush_send_queue();
}

void WebSocketConnection::on_transport_fragment(const uint8_t* data, size_t len, bool is_final) {
    // An oversized message is dropped as a whole: its remaining fragments too,
    // not glued into a truncated message.
    if (dropping_oversized_) {
        if (is_final) dropping_oversized_ = false;
        return;
    }
    if (data && len > 0) {
        if (reassembly_buffer_.size() + len > max_message_bytes_) {
            ++telemetry_.total_reassembly_overflow;
            if (logger_) logger_->log(LogLevel::Error, "WebSocketConnection: message exceeds max size, dropping");
            reassembly_buffer_.clear();
            dropping_oversized_ = !is_final;
            return;
        }
        reassembly_buffer_.append(reinterpret_cast<const char*>(data), len);
    }
    if (!is_final) return;

    ReceivedMessage msg{std::move(reassembly_buffer_), epoch_};
    reassembly_buffer_.clear();
    ++telemetry_.total_received;
    if (on_message_) on_message_(msg);
}

void WebSocketConnection::on_transport_writable() {
    writable_ = true;
    flush_send_queue();
}

void WebSocketConnection::on_transport_error(const char* reason) {
    if (logger_) logger_->log(LogLevel::Warn, reason ? reason : "WebSocketConnection: transport error");
    handle_disconnect(reason);
}

void WebSocketConnection::on_transport_closed(const char* reason) {
    handle_disconnect(reason);
}

void WebSocketConnection::handle_disconnect(const char* /*reason*/) {
    if (state_ == ConnectionState::Stopped) return; // already handled

    writable_ = false;
    send_queue_.clear();
    reassembly_buffer_.clear();
    telemetry_.current_queue_depth = 0;

    if (stopped_explicitly_) {
        set_state(ConnectionState::Stopped);
        return;
    }

    uint32_t delay = backoff_.delay_for_attempt(reconnect_attempt_++, jitter_source_());
    reconnect_at_ms_ = clock_.now_ms() + delay;
    set_state(ConnectionState::BackoffWait);
}

} // namespace app::net
