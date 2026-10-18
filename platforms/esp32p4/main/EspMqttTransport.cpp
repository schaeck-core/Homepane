#include "EspMqttTransport.h"

#include "esp_log.h"
#include "mqtt_client.h"

namespace platform_esp32p4 {

namespace {
const char* TAG = "mqtt";
constexpr size_t kMaxQueued = 64;
} // namespace

EspMqttTransport::~EspMqttTransport() { disconnect(); }

void EspMqttTransport::connect(const app::mqtt::MqttConfig& config)
{
    disconnect();
    config_ = config;
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = config_.host.c_str();
    cfg.broker.address.port = config_.port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = config_.client_id.c_str();
    if (!config_.username.empty()) cfg.credentials.username = config_.username.c_str();
    if (!config_.password.empty()) cfg.credentials.authentication.password = config_.password.c_str();
    if (!config_.will_topic.empty()) {
        cfg.session.last_will.topic = config_.will_topic.c_str();
        cfg.session.last_will.msg = config_.will_payload.c_str();
        cfg.session.last_will.msg_len = static_cast<int>(config_.will_payload.size());
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = config_.will_retain ? 1 : 0;
    }
    cfg.session.keepalive = 30;
    cfg.network.reconnect_timeout_ms = 10000;
    cfg.outbox.limit = 64 * 1024; // drop rather than grow without bound while offline
    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }
    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &EspMqttTransport::event_handler, this);
    esp_mqtt_client_start(client_);
}

void EspMqttTransport::disconnect()
{
    if (!client_) return;
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
}

bool EspMqttTransport::publish(const std::string& topic, const std::string& payload, bool retain, int qos)
{
    if (!client_) return false;
    // store=true: queued in the outbox, sent by the MQTT task.
    return esp_mqtt_client_enqueue(client_, topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos,
                                   retain ? 1 : 0, true) >= 0;
}

bool EspMqttTransport::subscribe(const std::string& topic_filter, int qos)
{
    if (!client_) return false;
    return esp_mqtt_client_subscribe_single(client_, topic_filter.c_str(), qos) >= 0;
}

void EspMqttTransport::enqueue(Event e)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() >= kMaxQueued) events_.pop_front(); // the UI is stuck: keep the newest
    events_.push_back(std::move(e));
}

void EspMqttTransport::event_handler(void* arg, const char*, int32_t id, void* data)
{
    auto* self = static_cast<EspMqttTransport*>(arg);
    auto* ev = static_cast<esp_mqtt_event_handle_t>(data);
    switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED: self->enqueue({Event::Connected, "", ""}); break;
    case MQTT_EVENT_DISCONNECTED: self->enqueue({Event::Disconnected, "connection lost", ""}); break;
    case MQTT_EVENT_ERROR:
        if (ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGW(TAG, "broker refused the connection (code %d): check user/password",
                     ev->error_handle->connect_return_code);
        }
        break;
    case MQTT_EVENT_DATA:
        // Large payloads come in pieces; commands are small, but be correct.
        if (ev->current_data_offset == 0) {
            self->partial_topic_.assign(ev->topic, ev->topic_len);
            self->partial_data_.clear();
        }
        self->partial_data_.append(ev->data, ev->data_len);
        if (ev->current_data_offset + ev->data_len >= ev->total_data_len) {
            self->enqueue({Event::Message, self->partial_topic_, self->partial_data_});
        }
        break;
    default: break;
    }
}

void EspMqttTransport::dispatch_pending()
{
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.swap(events_);
    }
    if (!sink_) return;
    for (const Event& e : events) {
        switch (e.kind) {
        case Event::Connected: sink_->on_mqtt_connected(); break;
        case Event::Disconnected: sink_->on_mqtt_disconnected(e.a.c_str()); break;
        case Event::Message: sink_->on_mqtt_message(e.a, e.b); break;
        }
    }
}

} // namespace platform_esp32p4
