#ifndef PLATFORMS_ESP32P4_MAIN_ESPWEBSOCKETTRANSPORT_H
#define PLATFORMS_ESP32P4_MAIN_ESPWEBSOCKETTRANSPORT_H

#include "../../../app/cpp/net/IWebSocketTransport.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

typedef struct esp_websocket_client* esp_websocket_client_handle_t;

namespace platform_esp32p4 {

// ESP32-P4 IWebSocketTransport adapter around esp_websocket_client.
//
// Unlike the legacy multi_ws_event_handler() in websocket_client_esp.c
// ("For simplicity, we parse each data event directly ... skip until
// final" — which discards every intermediate fragment and never
// correctly reassembles multi-fragment messages), this adapter forwards
// every WEBSOCKET_EVENT_DATA chunk with its is-final status up through
// on_transport_fragment() and leaves reassembly to WebSocketConnection,
// exactly like LwsTransport does on the simulator side. Same contract,
// two platform-appropriate implementations (CPP_MIGRATION_PLAN.md
// Phase 3).
//
// Threading: esp_websocket_client invokes its event handler on its own
// FreeRTOS task. The sink chain (WebSocketConnection -> HaClient ->
// DashboardController -> LVGL widgets) is not thread-safe, so events are
// only *buffered* on that task and delivered to the sink later from
// dispatch_pending(), which WebSocketConnection::poll() calls on the UI
// thread. Never call the sink directly from handle_event().
class EspWebSocketTransport final : public app::net::IWebSocketTransport {
public:
    EspWebSocketTransport();
    ~EspWebSocketTransport() override;

    EspWebSocketTransport(const EspWebSocketTransport&) = delete;
    EspWebSocketTransport& operator=(const EspWebSocketTransport&) = delete;

    void set_sink(app::net::IWebSocketTransportSink* sink) override;
    void connect(const std::string& url) override;
    void close() override;
    bool send_text(const uint8_t* data, size_t len) override;
    bool send_binary(const uint8_t* data, size_t len) override;

    // esp_websocket_client has no distinct "writable" notification: sends
    // are attempted with a bounded timeout (see .cpp) rather than gated
    // behind an explicit ready-to-write event. request_writable() is a
    // no-op here; WebSocketConnection's own queue-and-retry-on-next-post
    // behavior is what actually recovers from a transient failed send.
    void request_writable() override;

    void dispatch_pending() override;

    // Public only so the free function in the .cpp (matching
    // esp_event_handler_t's exact signature) can forward to it.
    static void event_trampoline(void* handler_arg, const char* event_base, int32_t event_id, void* event_data);

private:
    enum class EventKind { Open, Fragment, Error, Closed };
    struct PendingEvent {
        EventKind kind;
        uint32_t generation;
        std::string data;   // fragment bytes, or error/close reason
        bool is_final = false;
    };

    // Runs on esp_websocket_client's task: only touches queue_ (under mutex_).
    void handle_event(int32_t event_id, void* event_data);
    void enqueue(PendingEvent event);
    void destroy_handle();

    app::net::IWebSocketTransportSink* sink_ = nullptr;
    esp_websocket_client_handle_t handle_ = nullptr;

    std::mutex mutex_;
    std::deque<PendingEvent> queue_;   // guarded by mutex_
    // Bumped on every connect()/close(); events tagged with an older
    // generation (from a torn-down client) are dropped, never delivered.
    // Written only on the UI thread; read on the client task under mutex_.
    uint32_t generation_ = 0;
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPWEBSOCKETTRANSPORT_H
