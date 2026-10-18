// End-to-end integration test for Phase 8 of CPP_MIGRATION_PLAN.md: the
// first interactive EntityWidget (LightWidget) — optimistic UI, real
// server confirmation/rollback, generation-safe command cancellation —
// through the full real stack down to real LVGL objects and simulated
// taps. Only the network socket is faked.
//
// Matches the legacy app/widgets/light.c visually: a big icon colored by
// the light's rgb color (gray when off), no on-tile "On"/"Off" text —
// so unlike the pre-parity version of this test, state here is read back
// via the icon's own text_color (child index 1, fixed regardless of
// on/off — the optional radial glow, present only when on, is always
// created *after* the icon and *before* the name label, so it never
// shifts the icon's own index).

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/dashboard/DefaultWidgetRegistration.h"
#include "app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "app/cpp/home_assistant/HaCommandService.h"
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
    lv_display_t* disp = lv_display_create(720, 720);
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

// LightWidget's tile (create() in LightWidget.cpp, design guide centred
// tile): tile -> [0]=center -> [0]=icon circle -> [0]=icon, [1]=texts ->
// [0]=name, [1]=state. Off: the icon in text 3 (UI_TEXT3), on: sand or the
// light's colour.
constexpr uint32_t kOffColor = 0x8A857D;

lv_obj_t* center_of(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* root = dashboard_test::widget_root_at(session_root, widget_index);
    return root ? lv_obj_get_child(root, 0) : nullptr;
}

bool is_effectively_on(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* center = center_of(session_root, widget_index);
    lv_obj_t* icon = center ? lv_obj_get_child(lv_obj_get_child(center, 0), 0) : nullptr;
    if (!icon) return false;
    uint32_t color = lv_color_to_int(lv_obj_get_style_text_color(icon, LV_PART_MAIN));
    return color != kOffColor;
}

std::string name_label_text(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* center = center_of(session_root, widget_index);
    if (center) lv_obj_update_layout(center); // "..."-cut labels get their real width first
    lv_obj_t* name_label = center ? lv_obj_get_child(lv_obj_get_child(center, 1), 0) : nullptr;
    return name_label ? lv_label_get_text(name_label) : "<no name label>";
}

// A tap: the light waits for a possible double tap (100 % neutral) first.
void tap(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* widget_root = dashboard_test::widget_root_at(session_root, widget_index);
    lv_obj_send_event(widget_root, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_tick_inc(400);
    lv_timer_handler();
}

DashboardModel make_two_light_cards_model(const std::string& entity_id) {
    DashboardModel model;
    DashboardParser::ParseError err;
    std::string json = R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
                        R"({"type":"light","entity":")" +
                        entity_id + R"("},)"
                        R"({"type":"light","entity":")" +
                        entity_id + R"("})"
                        R"(]}]}]})";
    DashboardParser::parse(json, model, err);
    return model;
}

void snapshot(Fixture& fx, const std::string& entity_id, const std::string& state) {
    fx.transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{")" + entity_id + R"(":{"s":")" + state +
            R"(","a":{"friendly_name":"Test Light"},"lc":1000.0}}}})",
        true);
}

void test_tap_sends_correct_service_call_and_toggles_immediately() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(!is_effectively_on(session_root, 0), "widget shows the real confirmed off state before any tap");
    expect(name_label_text(session_root, 0) == "Test Light", "the name label shows the entity's friendly_name");

    tap(session_root, 0);

    expect(count_occurrences(fx->transport->sent_messages, "\"domain\":\"light\"") == 1,
           "tapping sends exactly one call_service message");
    expect(count_occurrences(fx->transport->sent_messages, "\"service\":\"turn_on\"") == 1,
           "tapping an off light calls turn_on, not turn_off");
    expect(fx->transport->sent_messages.back().find("\"entity_id\":\"light.x\"") != std::string::npos,
           "the service call targets the correct entity_id");
    expect(is_effectively_on(session_root, 0),
           "the tapped widget shows the optimistic 'on' appearance immediately, before any Result arrives");

    session.shutdown();
}

void test_successful_command_is_confirmed_by_real_update_not_left_pending_forever() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0);
    expect(is_effectively_on(session_root, 0), "on immediately after tap");

    // HA acknowledges the call_service request itself...
    fx->transport->simulate_fragment(R"({"id":2,"type":"result","success":true,"result":{}})", true);
    expect(is_effectively_on(session_root, 0),
           "the call_service Result alone does not flip anything — still the same optimistic 'on'");

    // ...and shortly after, the real entity_id delta confirming it arrives.
    fx->transport->simulate_fragment(R"({"id":1,"type":"event","event":{"c":{"light.x":{"+":{"s":"on","lc":1002.0}}}}})",
                                      true);
    expect(is_effectively_on(session_root, 0), "the real confirming update leaves the widget showing 'on'");

    session.shutdown();
}

void test_failed_command_rolls_back_immediately() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0);
    expect(is_effectively_on(session_root, 0), "on immediately after tap");

    fx->transport->simulate_fragment(
        R"({"id":2,"type":"result","success":false,"error":{"code":"unknown_error","message":"nope"}})", true);
    expect(!is_effectively_on(session_root, 0),
           "a failed call_service Result rolls back to the last confirmed (off) state, not stuck on");

    session.shutdown();
}

void test_timeout_rolls_back() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0);
    expect(is_effectively_on(session_root, 0), "on immediately after tap");

    fx->clock.advance(11000); // past HaClient's default 10s command timeout
    fx->client->poll(fx->clock.now_ms());

    expect(!is_effectively_on(session_root, 0),
           "a timed-out call_service command rolls back just like an explicit failure");

    session.shutdown();
}

void test_second_widget_unaffected_by_first_widgets_optimistic_tap() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0); // only the FIRST widget is tapped

    expect(is_effectively_on(session_root, 0), "the tapped widget shows the optimistic 'on' appearance");
    expect(!is_effectively_on(session_root, 1),
           "the untapped second widget for the same entity is not optimistically changed by a sibling's tap");

    // Once the real update arrives, both converge on the same true state.
    fx->transport->simulate_fragment(R"({"id":1,"type":"event","event":{"c":{"light.x":{"+":{"s":"on","lc":1002.0}}}}})",
                                      true);
    expect(is_effectively_on(session_root, 0) && is_effectively_on(session_root, 1),
           "both widgets converge on the same real state once the store updates, regardless of which one was tapped");

    session.shutdown();
}

void test_shutdown_during_in_flight_command_is_crash_free() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_light_cards_model("light.x");
    auto session = std::make_unique<DashboardSession>(std::move(model), fx->factory,
                                                        app::core::DashboardGeneration(1), fx->coordinator.get());
    session->create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "light.x", "off");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0); // starts an in-flight call_service, id=2

    session->shutdown(); // dashboard switch while the command is still pending
    session.reset();

    // The (now stale) Result for the cancelled command arrives after the
    // widget is gone — HaCommandService/HaClient must not call back into
    // freed widget memory.
    fx->transport->simulate_fragment(R"({"id":2,"type":"result","success":true,"result":{}})", true);
    expect(true, "a Result arriving for a command whose widget was torn down mid-flight does not crash");
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_tap_sends_correct_service_call_and_toggles_immediately();
    test_successful_command_is_confirmed_by_real_update_not_left_pending_forever();
    test_failed_command_rolls_back_immediately();
    test_timeout_rolls_back();
    test_second_widget_unaffected_by_first_widgets_optimistic_tap();
    test_shutdown_during_in_flight_command_is_crash_free();

    if (g_failures == 0) {
        std::printf("All LightWidget end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
