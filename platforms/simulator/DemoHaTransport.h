#ifndef PLATFORMS_SIMULATOR_DEMOHATRANSPORT_H
#define PLATFORMS_SIMULATOR_DEMOHATRANSPORT_H

#include "../../app/cpp/net/IWebSocketTransport.h"

#include <cjson/cJSON.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace platform_simulator {

// A Home Assistant stand-in for the simulator (HOMEPANE_DEMO=1): answers
// the panel's WebSocket protocol from the fixtures in platforms/simulator/demo
// (dashboard, entity states) and applies service calls to those states, so
// every tile type can be designed and tried without a real HA.
class DemoHaTransport final : public app::net::IWebSocketTransport {
public:
    explicit DemoHaTransport(std::string fixture_dir);
    ~DemoHaTransport() override;

    void set_sink(app::net::IWebSocketTransportSink* sink) override { sink_ = sink; }
    void connect(const std::string& url) override;
    void close() override;
    bool send_text(const uint8_t* data, size_t len) override;
    void request_writable() override { queue_writable_ = true; }
    void dispatch_pending() override;

private:
    void handle(cJSON* msg);
    void call_service(const std::string& domain, const std::string& service, cJSON* data);
    void push_change(const std::string& entity_id);
    void reply(int id, cJSON* result_owned);
    void reply_error(int id, const char* code, const char* message);
    void event(int id, cJSON* event_owned);
    cJSON* entity(const std::string& entity_id);
    void set_attr(cJSON* ent, const char* key, cJSON* value_owned);

    std::string fixture_dir_;
    app::net::IWebSocketTransportSink* sink_ = nullptr;
    std::deque<std::string> inbox_; // messages to deliver on the next dispatch_pending()
    bool open_pending_ = false;
    bool closed_pending_ = false;
    bool queue_writable_ = false;
    cJSON* states_ = nullptr; // {entity_id: {"s": ..., "a": {...}}}
    std::map<int, std::vector<std::string>> subscriptions_; // subscribe_entities id -> entity ids
    // todo.einkaufsliste: uid, summary, completed
    struct TodoItem {
        std::string uid, summary;
        bool done = false;
    };
    std::vector<TodoItem> todo_ = {{"1", "Milk"}, {"2", "Bread"}, {"3", "Apples"}, {"4", "Coffee"}, {"5", "Dish soap"}};
    cJSON* calendar_events() const;
};

} // namespace platform_simulator

#endif // PLATFORMS_SIMULATOR_DEMOHATRANSPORT_H
