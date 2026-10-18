#include "EspWebSocketTransport.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <utility>
#include <vector>

namespace platform_esp32p4 {

namespace {
const char* TAG = "ws_transport";

// Matches esp_event_handler_t exactly (esp_event_base_t is `const char*`),
// forwards to the header-friendly trampoline so EspWebSocketTransport.h
// does not need to include esp_event.h.
void raw_event_handler(void* handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    EspWebSocketTransport::event_trampoline(handler_arg, event_base, event_id, event_data);
}

// esp_websocket_client_stop()/destroy() block until the client task has
// exited, which can take many seconds (close handshake, poll timeouts).
// Doing that on the UI thread froze the whole app while holding the LVGL
// lock — long enough that a *new* connection opened right before missed
// Home Assistant's 10 s auth deadline (seen after OAuth pairing: the
// pairing connection's teardown stalled the setup connection's auth
// reply). So teardown runs on a short-lived helper task instead.
void reaper_task(void* arg) {
    auto handle = static_cast<esp_websocket_client_handle_t>(arg);
    esp_websocket_client_destroy(handle); // stops the client task first if still running
    vTaskDelete(nullptr);
}
} // namespace

EspWebSocketTransport::EspWebSocketTransport() = default;

EspWebSocketTransport::~EspWebSocketTransport() { close(); }

void EspWebSocketTransport::set_sink(app::net::IWebSocketTransportSink* sink) { sink_ = sink; }

void EspWebSocketTransport::destroy_handle() {
    if (!handle_) return;
    esp_websocket_client_handle_t handle = handle_;
    handle_ = nullptr;

    // Detach first: once this returns, the client task will never call
    // back into `this` again (esp_event serializes unregister against an
    // in-flight dispatch), so the handle can be torn down asynchronously
    // even if this transport is destroyed right after. Every caller
    // (connect/close/destructor) runs on the UI thread, never on the
    // client task.
    esp_websocket_unregister_events(handle, WEBSOCKET_EVENT_ANY, raw_event_handler);

    if (xTaskCreate(reaper_task, "ws_reaper", 4096, handle, 5, nullptr) != pdPASS) {
        ESP_LOGW(TAG, "could not spawn teardown task, tearing down synchronously");
        esp_websocket_client_destroy(handle);
    }
}

void EspWebSocketTransport::connect(const std::string& url) {
    // A reconnect (WebSocketConnection::poll() after BackoffWait) calls
    // connect() again on the same transport: tear the previous client down
    // first instead of leaking its handle and task.
    destroy_handle();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        queue_.clear();
    }

    esp_websocket_client_config_t cfg = {};
    cfg.uri = url.c_str();
    cfg.buffer_size = 4096;
    cfg.task_stack = 6144; // internal RAM: ~2 KB used at most (measured), one per connection
    cfg.reconnect_timeout_ms = 30000;
    cfg.network_timeout_ms = 30000;
    // Reconnect is handled by WebSocketConnection (epoch, backoff), not by
    // the transport itself, so results stay attributable to a known
    // epoch. esp_websocket_client's own auto-reconnect would otherwise
    // silently re-establish without WebSocketConnection ever knowing an
    // epoch boundary should have been crossed.
    cfg.disable_auto_reconnect = true;

    ESP_LOGI(TAG, "connecting to %s", url.c_str());

    handle_ = esp_websocket_client_init(&cfg);
    if (!handle_) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        enqueue({EventKind::Error, generation_, "esp_websocket_client_init failed"});
        return;
    }

    esp_err_t err = esp_websocket_register_events(handle_, WEBSOCKET_EVENT_ANY, raw_event_handler, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_register_events failed: %s", esp_err_to_name(err));
        destroy_handle();
        enqueue({EventKind::Error, generation_, "esp_websocket_register_events failed"});
        return;
    }

    err = esp_websocket_client_start(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
        destroy_handle();
        enqueue({EventKind::Error, generation_, "esp_websocket_client_start failed"});
    }
}

void EspWebSocketTransport::close() {
    destroy_handle();
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    queue_.clear();
}

bool EspWebSocketTransport::send_text(const uint8_t* data, size_t len) {
    if (!handle_) return false;
    // Bounded timeout, not portMAX_DELAY: a UI-context caller must never
    // block indefinitely on a network send (docs/ARCHITECTURE.md §9).
    int sent = esp_websocket_client_send_text(handle_, reinterpret_cast<const char*>(data),
                                               static_cast<int>(len), pdMS_TO_TICKS(200));
    if (sent != static_cast<int>(len)) {
        ESP_LOGW(TAG, "send_text failed (%d of %u bytes)", sent, static_cast<unsigned>(len));
        // The library does not always emit DISCONNECTED when the peer has
        // already gone (e.g. HA closing an unauthenticated socket). Without
        // this, WebSocketConnection would keep the message queued forever
        // with nothing ever retrying it; report the dead connection so it
        // backs off and reconnects with a fresh handshake instead.
        if (!esp_websocket_client_is_connected(handle_)) {
            enqueue({EventKind::Closed, generation_, "send failed: not connected"});
        }
        return false;
    }
    return true;
}

bool EspWebSocketTransport::send_binary(const uint8_t* data, size_t len) {
    if (!handle_) return false;
    int sent = esp_websocket_client_send_bin(handle_, reinterpret_cast<const char*>(data), static_cast<int>(len),
                                             pdMS_TO_TICKS(200));
    if (sent != static_cast<int>(len)) {
        ESP_LOGW(TAG, "send_binary failed (%d of %u bytes)", sent, static_cast<unsigned>(len));
        if (!esp_websocket_client_is_connected(handle_)) {
            enqueue({EventKind::Closed, generation_, "send failed: not connected"});
        }
        return false;
    }
    return true;
}

void EspWebSocketTransport::request_writable() {
    // No distinct writable notification on this platform; see header
    // comment. WebSocketConnection retries on the next send()/poll().
}

void EspWebSocketTransport::enqueue(PendingEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(event));
}

void EspWebSocketTransport::dispatch_pending() {
    std::deque<PendingEvent> ready;
    uint32_t current_generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(queue_);
        current_generation = generation_;
    }

    for (const PendingEvent& ev : ready) {
        // A sink callback may close() or reconnect this transport (e.g.
        // HaClient on auth_invalid); anything left over from before that
        // belongs to a dead connection and must not be delivered.
        if (!sink_ || ev.generation != current_generation || ev.generation != generation_) continue;

        switch (ev.kind) {
            case EventKind::Open:
                sink_->on_transport_open();
                break;
            case EventKind::Fragment:
                sink_->on_transport_fragment(reinterpret_cast<const uint8_t*>(ev.data.data()), ev.data.size(),
                                              ev.is_final);
                break;
            case EventKind::Error:
                sink_->on_transport_error(ev.data.c_str());
                break;
            case EventKind::Closed:
                sink_->on_transport_closed(ev.data.c_str());
                break;
        }
    }
}

void EspWebSocketTransport::event_trampoline(void* handler_arg, const char* /*event_base*/,
                                              int32_t event_id, void* event_data) {
    auto* self = reinterpret_cast<EspWebSocketTransport*>(handler_arg);
    if (self) self->handle_event(event_id, event_data);
}

void EspWebSocketTransport::handle_event(int32_t event_id, void* event_data) {
    auto* data = reinterpret_cast<esp_websocket_event_data_t*>(event_data);

    uint32_t gen;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        gen = generation_;
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected");
            enqueue({EventKind::Open, gen, {}});
            break;

        case WEBSOCKET_EVENT_DATA: {
            // Ping/pong/close control frames carry no application payload.
            if (data->op_code == 0x08 || data->op_code == 0x09 || data->op_code == 0x0A) break;
            if (data->data_len <= 0) break;
            // A logical message ends with the last chunk of a frame that
            // has FIN set (continuation frames carry FIN=0 until the end).
            bool frame_done = (data->payload_offset + data->data_len) >= data->payload_len;
            PendingEvent ev{EventKind::Fragment, gen, std::string(data->data_ptr, data->data_len)};
            ev.is_final = frame_done && data->fin;
            enqueue(std::move(ev));
            break;
        }

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGW(TAG, "error event");
            enqueue({EventKind::Error, gen, "esp_websocket_client error"});
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "disconnected");
            enqueue({EventKind::Closed, gen, "disconnected"});
            break;

        default:
            break;
    }
}

} // namespace platform_esp32p4
