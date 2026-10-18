#ifndef PLATFORMS_SIMULATOR_LWSTRANSPORT_H
#define PLATFORMS_SIMULATOR_LWSTRANSPORT_H

#include "../../app/cpp/net/IWebSocketTransport.h"

struct lws_context;
struct lws;

namespace platform_simulator {

// Simulator IWebSocketTransport adapter around libwebsockets. Each
// instance owns its own lws_context (mirrors the existing per-client
// context pattern in app/websocket_client.c) so distinct named
// connections (HA, Music Assistant, ...) are fully isolated.
//
// Unlike the legacy callback_ws_multi() in app/websocket_client.c, this
// adapter forwards every fragment (checking lws_is_final_fragment()) up
// through on_transport_fragment() instead of assuming `in` is always a
// complete message — reassembly across fragments is WebSocketConnection's
// job, not this adapter's (CPP_MIGRATION_PLAN.md Phase 3).
class LwsTransport final : public app::net::IWebSocketTransport {
public:
    LwsTransport();
    ~LwsTransport() override;

    LwsTransport(const LwsTransport&) = delete;
    LwsTransport& operator=(const LwsTransport&) = delete;

    void set_sink(app::net::IWebSocketTransportSink* sink) override;
    void connect(const std::string& url) override;
    void close() override;
    bool send_text(const uint8_t* data, size_t len) override;
    bool send_binary(const uint8_t* data, size_t len) override;
    void request_writable() override;

    // Must be called periodically (e.g. from the same loop that already
    // calls ws_service_all()) to pump this connection's lws_context.
    void service();

    // Public only so the free function in LwsTransport.cpp (matching lws'
    // exact C callback signature) can forward to it. Not part of the
    // adapter's real API — do not call this directly.
    static int lws_callback_trampoline(struct lws* wsi, int reason, void* user, void* in, size_t len);

private:
    void handle_callback(struct lws* wsi, int reason, void* in, size_t len);

    app::net::IWebSocketTransportSink* sink_ = nullptr;
    struct lws_context* context_ = nullptr;
    struct lws* wsi_ = nullptr;
    bool closing_ = false;
};

} // namespace platform_simulator

#endif // PLATFORMS_SIMULATOR_LWSTRANSPORT_H
