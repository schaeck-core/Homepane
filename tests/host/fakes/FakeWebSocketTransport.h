#ifndef TESTS_HOST_FAKES_FAKEWEBSOCKETTRANSPORT_H
#define TESTS_HOST_FAKES_FAKEWEBSOCKETTRANSPORT_H

#include "../../../app/cpp/net/IWebSocketTransport.h"

#include <string>
#include <vector>

namespace test_fakes {

// Test double: no sockets, no threads. Lets contract tests drive
// WebSocketConnection through exact byte-level scenarios (fragmentation,
// partial writes, disconnects) deterministically.
class FakeWebSocketTransport final : public app::net::IWebSocketTransport {
public:
    void set_sink(app::net::IWebSocketTransportSink* sink) override { sink_ = sink; }

    void connect(const std::string& url) override { connect_calls.push_back(url); }
    void close() override { ++close_calls; }

    bool send_text(const uint8_t* data, size_t len) override {
        sent_messages.emplace_back(reinterpret_cast<const char*>(data), len);
        return send_should_succeed;
    }

    bool send_binary(const uint8_t* data, size_t len) override {
        if (!send_should_succeed) return false;
        sent_binary.emplace_back(reinterpret_cast<const char*>(data), len);
        return true;
    }
    void request_writable() override { ++request_writable_calls; }

    // --- test-side simulation helpers ---
    void simulate_open() { if (sink_) sink_->on_transport_open(); }
    void simulate_fragment(const std::string& data, bool is_final) {
        if (sink_) {
            sink_->on_transport_fragment(reinterpret_cast<const uint8_t*>(data.data()), data.size(), is_final);
        }
    }
    void simulate_error(const char* reason) { if (sink_) sink_->on_transport_error(reason); }
    void simulate_closed(const char* reason) { if (sink_) sink_->on_transport_closed(reason); }
    void simulate_writable() { if (sink_) sink_->on_transport_writable(); }

    std::vector<std::string> connect_calls;
    int close_calls = 0;
    std::vector<std::string> sent_messages;
    std::vector<std::string> sent_binary;
    bool send_should_succeed = true;
    int request_writable_calls = 0;

private:
    app::net::IWebSocketTransportSink* sink_ = nullptr;
};

} // namespace test_fakes

#endif // TESTS_HOST_FAKES_FAKEWEBSOCKETTRANSPORT_H
