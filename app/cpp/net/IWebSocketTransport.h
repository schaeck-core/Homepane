#ifndef APP_CPP_NET_IWEBSOCKETTRANSPORT_H
#define APP_CPP_NET_IWEBSOCKETTRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace app::net {

// Transport-level events. Carries no HA/MA message knowledge — just bytes
// and connection lifecycle (CPP_MIGRATION_PLAN.md §3.1: "IWebSocketTransport
// ... kennt keine HA-Nachrichtentypen").
class IWebSocketTransportSink {
public:
    virtual ~IWebSocketTransportSink() = default;

    virtual void on_transport_open() = 0;

    // One fragment of an incoming message. `is_final` marks the last
    // fragment of a logical message. Reassembly is the sink's job
    // (WebSocketConnection), not the transport's — fragmentation
    // handling differs per platform library, but reassembly should not
    // be duplicated per platform.
    virtual void on_transport_fragment(const uint8_t* data, size_t len, bool is_final) = 0;

    virtual void on_transport_error(const char* reason) = 0;
    virtual void on_transport_closed(const char* reason) = 0;

    // The transport is ready to accept send_text() again after either an
    // initial connect or a previously-refused/partial write.
    virtual void on_transport_writable() = 0;
};

// Thin per-platform I/O shim: bytes in, bytes out, lifecycle events out.
// Never touches LVGL. Simulator: libwebsockets (LwsTransport). ESP32-P4:
// esp_websocket_client (EspWebSocketTransport). Both must satisfy the same
// contract tests (CPP_MIGRATION_PLAN.md Phase 3 DoD).
class IWebSocketTransport {
public:
    virtual ~IWebSocketTransport() = default;

    // Sink must outlive the transport, or be cleared via set_sink(nullptr)
    // before it is destroyed.
    virtual void set_sink(IWebSocketTransportSink* sink) = 0;

    virtual void connect(const std::string& url) = 0;
    virtual void close() = 0;

    // Returns false if the transport is not currently able to accept the
    // write (caller must queue and wait for on_transport_writable()).
    virtual bool send_text(const uint8_t* data, size_t len) = 0;

    // Binary frame (e.g. microphone audio for the HA Assist pipeline). Same
    // contract as send_text(); transports without binary support refuse.
    virtual bool send_binary(const uint8_t* data, size_t len)
    {
        (void)data;
        (void)len;
        return false;
    }

    // Asks the transport to fire on_transport_writable() once sending is
    // possible again. Transports that are always immediately writable may
    // treat this as invoking the sink on the next poll/service call.
    virtual void request_writable() = 0;

    // Delivers any sink events the transport has buffered since the last
    // call. Called by WebSocketConnection::poll(), i.e. always on the UI
    // thread. Transports whose library already invokes callbacks on the
    // polling thread (LwsTransport, test fakes) leave this a no-op;
    // transports whose library calls back on its own task
    // (EspWebSocketTransport) must buffer there and deliver here instead
    // — the sink chain (WebSocketConnection -> HaClient -> dashboard ->
    // LVGL) is not thread-safe.
    virtual void dispatch_pending() {}
};

} // namespace app::net

#endif // APP_CPP_NET_IWEBSOCKETTRANSPORT_H
