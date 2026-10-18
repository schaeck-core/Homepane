#ifndef APP_CPP_MQTT_IMQTTTRANSPORT_H
#define APP_CPP_MQTT_IMQTTTRANSPORT_H

#include <cstdint>
#include <string>

namespace app::mqtt {

struct MqttConfig {
    std::string host;
    uint16_t port = 1883;
    std::string username, password;
    std::string client_id;
    // Last will: published by the broker when the connection drops.
    std::string will_topic, will_payload;
    bool will_retain = true;
};

class IMqttSink {
public:
    virtual ~IMqttSink() = default;
    virtual void on_mqtt_connected() = 0;
    virtual void on_mqtt_disconnected(const char* reason) = 0;
    virtual void on_mqtt_message(const std::string& topic, const std::string& payload) = 0;
};

// MQTT sibling of IWebSocketTransport (docs/ARCHITECTURE.md §13.4): bytes
// and lifecycle only, no Home Assistant knowledge. ESP32-P4: esp-mqtt.
// Like the WebSocket transport, events from the library's own task are
// buffered and delivered to the sink in dispatch_pending() on the UI thread.
class IMqttTransport {
public:
    virtual ~IMqttTransport() = default;
    virtual void set_sink(IMqttSink* sink) = 0;
    virtual void connect(const MqttConfig& config) = 0; // reconnects by itself
    virtual void disconnect() = 0;
    virtual bool publish(const std::string& topic, const std::string& payload, bool retain, int qos = 0) = 0;
    virtual bool subscribe(const std::string& topic_filter, int qos = 0) = 0;
    virtual void dispatch_pending() {}
};

} // namespace app::mqtt

#endif // APP_CPP_MQTT_IMQTTTRANSPORT_H
