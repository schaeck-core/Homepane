#ifndef PLATFORMS_ESP32P4_MAIN_ESPMQTTTRANSPORT_H
#define PLATFORMS_ESP32P4_MAIN_ESPMQTTTRANSPORT_H

#include "../../../app/cpp/mqtt/IMqttTransport.h"

#include <deque>
#include <mutex>
#include <string>

struct esp_mqtt_client;

namespace platform_esp32p4 {

// IMqttTransport on esp-mqtt. Library events arrive on the MQTT task and are
// queued; dispatch_pending() (UI thread) hands them to the sink. Publishing
// only enqueues into esp-mqtt's outbox (esp_mqtt_client_enqueue): the UI
// thread never waits for the network. esp-mqtt reconnects by itself.
class EspMqttTransport final : public app::mqtt::IMqttTransport {
public:
    ~EspMqttTransport() override;
    void set_sink(app::mqtt::IMqttSink* sink) override { sink_ = sink; }
    void connect(const app::mqtt::MqttConfig& config) override;
    void disconnect() override;
    bool publish(const std::string& topic, const std::string& payload, bool retain, int qos) override;
    bool subscribe(const std::string& topic_filter, int qos) override;
    void dispatch_pending() override;

private:
    struct Event {
        enum Kind { Connected, Disconnected, Message } kind;
        std::string a, b; // topic / payload, or the reason
    };
    static void event_handler(void* arg, const char* base, int32_t id, void* data);
    void enqueue(Event e);

    app::mqtt::IMqttSink* sink_ = nullptr;
    esp_mqtt_client* client_ = nullptr;
    app::mqtt::MqttConfig config_; // esp-mqtt keeps pointers into these strings
    std::mutex mutex_;
    std::deque<Event> events_;
    std::string partial_topic_, partial_data_; // fragmented incoming messages
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPMQTTTRANSPORT_H
