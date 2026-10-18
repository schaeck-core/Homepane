// End-to-end integration test for Phase 7 of CPP_MIGRATION_PLAN.md: the
// first real EntityWidget (SensorWidget) driven through the FULL real
// stack — FakeWebSocketTransport -> WebSocketConnection -> HaClient ->
// EntityStore/EntitySubscriptionCoordinator -> WidgetFactory ->
// DashboardSession -> real headless LVGL objects. Nothing here is a mock
// of this project's own logic; only the network socket is faked.

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/dashboard/DefaultWidgetRegistration.h"
#include "app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "dashboard_test_helpers.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace app::dashboard;
using namespace app::home_assistant;
using app::core::IMonotonicClock;
using app::net::WebSocketConnection;
using test_fakes::FakeWebSocketTransport;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

class FakeClock final : public IMonotonicClock {
public:
    uint64_t now_ms() const override { return now_; }
    void advance(uint64_t ms) { now_ += ms; }
    uint64_t now_ = 1000;
};

void noop_flush_cb(lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); }

lv_obj_t* make_headless_screen() {
    lv_display_t* disp = lv_display_create(720, 720); // the panel: tiles at their real width
    static uint8_t buf[720 * 10 * 4];
    lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, noop_flush_cb);
    return lv_display_get_screen_active(disp);
}

int count_occurrences(const std::vector<std::string>& haystack, const std::string& needle) {
    int n = 0;
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) ++n;
    }
    return n;
}

struct Fixture {
    FakeClock clock;
    FakeWebSocketTransport* transport;
    std::unique_ptr<HaClient> client;
    EntityStore store;
    std::unique_ptr<EntitySubscriptionCoordinator> coordinator;
    std::unique_ptr<HaCommandService> commands;
    WidgetFactory factory;
};

std::unique_ptr<Fixture> make_ready_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    auto connection = std::make_unique<WebSocketConnection>("ha", std::move(transport), fx->clock);
    fx->client = std::make_unique<HaClient>(std::move(connection), fx->clock, "token");
    fx->coordinator = std::make_unique<EntitySubscriptionCoordinator>(fx->store, *fx->client,
                                                                       HaInstanceId{"homeassistant"}, fx->clock);
    fx->commands = std::make_unique<HaCommandService>(*fx->client);
    register_default_widgets(fx->factory, HaInstanceId{"homeassistant"}, *fx->commands);

    fx->client->start("ws://fake/api/websocket");
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx->coordinator->on_ha_ready();
    return fx;
}

// The value tile's texts (value, unit, status) as rendered, from the real
// LVGL tree: every label of the Nth widget except its name (the first),
// non-empty ones joined with " ". Widget internals stay private.
void collect_labels(lv_obj_t* obj, std::vector<std::string>& out) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        if (lv_obj_check_type(child, &lv_label_class)) out.emplace_back(lv_label_get_text(child));
        else collect_labels(child, out);
    }
}

std::string read_state_label_text(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* widget_root = dashboard_test::widget_root_at(session_root, widget_index);
    if (!widget_root) return "<no widget>";
    lv_obj_update_layout(widget_root); // "..."-cut labels get their real width first
    std::vector<std::string> labels;
    collect_labels(widget_root, labels);
    std::string text;
    for (size_t i = 1; i < labels.size(); ++i) { // [0] = name
        if (labels[i].empty()) continue;
        text += (text.empty() ? "" : " ") + labels[i];
    }
    return text;
}

DashboardModel make_two_sensor_tiles_model(const std::string& entity_id) {
    DashboardModel model;
    DashboardParser::ParseError err;
    // big tiles (rows 4): a tile card without a size is a compact tile
    std::string json = R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
                        R"({"type":"tile","entity":")" +
                        entity_id + R"(","grid_options":{"columns":12,"rows":4}},)"
                        R"({"type":"tile","entity":")" +
                        entity_id + R"(","grid_options":{"columns":12,"rows":4}})"
                        R"(]}]}]})";
    DashboardParser::parse(json, model, err);
    return model;
}

void test_two_widgets_same_entity_share_one_store_record_and_subscription() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_sensor_tiles_model("sensor.temp");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);

    expect(session.widget_count() == 2, "two tile cards for the same entity yield two real widget instances");
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1,
           "two SensorWidgets observing the same entity still result in exactly one subscribe_entities call");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(read_state_label_text(session_root, 0) == "…" && read_state_label_text(session_root, 1) == "…",
           "both widgets show the loading placeholder immediately after create(), before any network data");

    // Confirm the subscription.
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);

    // Real entity snapshot arrives.
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"sensor.temp":{"s":"21.5","a":{)"
        R"("unit_of_measurement":"°C","friendly_name":"Server Temp"},"lc":1000.0}}}})",
        true);

    expect(read_state_label_text(session_root, 0).rfind("21,5 °C", 0) == 0 &&
               read_state_label_text(session_root, 1).rfind("21,5 °C", 0) == 0,
           "both widgets update independently from the single underlying store record");
    expect(fx->store.diagnostics().entity_count == 1, "exactly one EntityStore record backs both widgets");

    // A follow-up delta must update both widgets again, not duplicate state.
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"sensor.temp":{"+":{"s":"22.1","lu":1001.0}}}}})", true);
    expect(read_state_label_text(session_root, 0).rfind("22,1 °C", 0) == 0 &&
               read_state_label_text(session_root, 1).rfind("22,1 °C", 0) == 0,
           "a delta after the snapshot updates both widgets to the same new value");

    session.shutdown();
}

void test_unavailable_and_stale_are_visible() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_sensor_tiles_model("sensor.flaky");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"sensor.flaky":{"s":"unavailable","a":{},"lc":1000.0}}}})", true);
    expect(read_state_label_text(session_root, 0) == "– Nicht verfügbar",
           "an 'unavailable' HA state is rendered as a distinct, human-readable label, not the raw string");

    // Recover, then go stale via disconnect.
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"sensor.flaky":{"+":{"s":"5","lu":1001.0}}}}})", true);
    expect(read_state_label_text(session_root, 0).rfind("5", 0) == 0 &&
               read_state_label_text(session_root, 0).find("eraltet") == std::string::npos,
           "a real value after recovery is shown plainly");

    fx->transport->simulate_closed("network blip");
    fx->coordinator->on_ha_disconnected();
    expect(read_state_label_text(session_root, 0).rfind("5 ", 0) == 0 &&
               read_state_label_text(session_root, 0).find("eraltet") != std::string::npos,
           "disconnect marks the widget stale while preserving the last known value, not blanking it");

    session.shutdown();
}

void test_dashboard_switch_unsubscribes_safely() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_sensor_tiles_model("sensor.temp");
    auto session = std::make_unique<DashboardSession>(std::move(model), fx->factory,
                                                        app::core::DashboardGeneration(1), fx->coordinator.get());
    session->create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    expect(fx->coordinator->diagnostics().active_remote_subscriptions == 1, "subscription active before switch");

    session->shutdown(); // dashboard switch: old session torn down
    session.reset();

    expect(lv_obj_get_child_count(screen) == 0, "switching away deletes the real LVGL subtree");

    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe_events") == 0,
           "losing both local observers at once still respects the debounce, not an immediate unsubscribe");

    fx->clock.advance(3000);
    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe_events") == 1,
           "after the debounce window, the dashboard switch results in exactly one real unsubscribe_events");

    // A late (simulated) event for the now-abandoned entity must not
    // crash even though every local widget is gone.
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"sensor.temp":{"+":{"s":"99","lu":2000.0}}}}})", true);
    expect(true, "a late event for an entity with zero remaining local observers does not crash");
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_two_widgets_same_entity_share_one_store_record_and_subscription();
    test_unavailable_and_stale_are_visible();
    test_dashboard_switch_unsubscribes_safely();

    if (g_failures == 0) {
        std::printf("All sensor tile end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
