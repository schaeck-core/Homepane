// End-to-end integration test of the climate tile (ClimateTile, design
// guide gauge tile): -/+ presses collected into one set_temperature,
// the new target shown until HA confirms, rollback on failure, heat_cool
// ranges, generation-safe command cancellation — through the full real
// stack down to real LVGL objects. Only the network socket is faked.

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/dashboard/DefaultWidgetRegistration.h"
#include "app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "app/cpp/home_assistant/HaCommandService.h"
#include "app/cpp/ui/tiles/ClimateTile.h"
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

// The tile's texts as rendered: every label of the Nth widget, joined
// with "|" (name|main|sub|...). Buttons: the tile's last child is the
// button row, [0] = minus, [1] = plus.
void collect_labels(lv_obj_t* obj, std::string& out) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        if (lv_obj_check_type(child, &lv_label_class)) out += std::string(lv_label_get_text(child)) + "|";
        else collect_labels(child, out);
    }
}

std::string texts(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* root = dashboard_test::widget_root_at(session_root, widget_index);
    if (!root) return "<no widget>";
    lv_obj_update_layout(root);
    std::string out;
    collect_labels(root, out);
    return out;
}

void press(lv_obj_t* session_root, uint32_t widget_index, int button) {
    lv_obj_t* root = dashboard_test::widget_root_at(session_root, widget_index);
    lv_obj_t* row = lv_obj_get_child(root, -1);
    lv_obj_send_event(lv_obj_get_child(row, button), LV_EVENT_CLICKED, nullptr);
}

// Lets the tile's send delay pass (LVGL timers).
void let_send_delay_pass() {
    lv_tick_inc(app::ui::ClimateTile::kSendDelayMs + 50);
    lv_timer_handler();
}

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

DashboardModel make_two_thermostat_cards_model(const std::string& entity_id) {
    DashboardModel model;
    DashboardParser::ParseError err;
    std::string json = R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
                        R"({"type":"thermostat","entity":")" +
                        entity_id + R"("},)"
                        R"({"type":"thermostat","entity":")" +
                        entity_id + R"("})"
                        R"(]}]}]})";
    DashboardParser::parse(json, model, err);
    return model;
}

void snapshot(Fixture& fx, const std::string& entity_id, const std::string& state, double current, double target) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  R"({"id":1,"type":"event","event":{"a":{"%s":{"s":"%s","a":{)"
                  R"("friendly_name":"Living Room","current_temperature":%.1f,"temperature":%.1f,)"
                  R"("hvac_action":"heating","min_temp":10,"max_temp":30},"lc":1000.0}}}})",
                  entity_id.c_str(), state.c_str(), current, target);
    fx.transport->simulate_fragment(buf, true);
}

void heat_cool_snapshot(Fixture& fx, const std::string& entity_id, double current, double low, double high) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  R"({"id":1,"type":"event","event":{"a":{"%s":{"s":"heat_cool","a":{)"
                  R"("friendly_name":"Office","current_temperature":%.1f,"target_temp_low":%.1f,)"
                  R"("target_temp_high":%.1f,"min_temp":7,"max_temp":30,"target_temp_step":0.5},"lc":1000.0}}}})",
                  entity_id.c_str(), current, low, high);
    fx.transport->simulate_fragment(buf, true);
}

struct Setup {
    std::unique_ptr<Fixture> fx;
    std::unique_ptr<DashboardSession> session;
    lv_obj_t* root = nullptr;
};

Setup make_setup(const std::string& entity_id = "climate.livingroom") {
    Setup s;
    s.fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();
    s.session = std::make_unique<DashboardSession>(make_two_thermostat_cards_model(entity_id), s.fx->factory,
                                                   app::core::DashboardGeneration(1), s.fx->coordinator.get());
    s.session->create(screen);
    s.fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    s.root = lv_obj_get_child(screen, 0);
    return s;
}

void test_two_widgets_share_subscription_and_render_initial_state() {
    Setup s = make_setup();
    expect(s.session->widget_count() == 2, "two thermostat cards for the same entity yield two real widget instances");
    expect(count_occurrences(s.fx->transport->sent_messages, "subscribe_entities") == 1,
           "two climate tiles observing the same entity still result in exactly one subscribe_entities call");
    snapshot(*s.fx, "climate.livingroom", "heat", 21.3, 22.0);
    const std::string t = texts(s.root, 0);
    expect(contains(t, "Living Room|") && contains(t, "|22°|") && contains(t, "|Heizt|"),
           "name, the target as the big number and the hvac action render from the real snapshot: " + t);
    s.session->shutdown();
}

void test_presses_are_collected_into_one_command() {
    Setup s = make_setup();
    snapshot(*s.fx, "climate.livingroom", "heat", 21.3, 22.0);
    const size_t before = s.fx->transport->sent_messages.size();
    press(s.root, 0, 1);
    press(s.root, 0, 1);
    press(s.root, 0, 1);
    expect(contains(texts(s.root, 0), "|23,5°|"), "each press shows the new target at once");
    expect(s.fx->transport->sent_messages.size() == before, "nothing is sent while the user is still pressing");

    let_send_delay_pass();
    expect(count_occurrences(s.fx->transport->sent_messages, "\"service\":\"set_temperature\"") == 1,
           "three presses: exactly one set_temperature after the pause");
    expect(contains(s.fx->transport->sent_messages.back(), "\"temperature\":23.5") &&
               contains(s.fx->transport->sent_messages.back(), "\"entity_id\":\"climate.livingroom\""),
           "it carries the final target for this entity");
    expect(contains(texts(s.root, 0), "|23,5°|"), "the new target stays while HA has not confirmed yet");
    expect(contains(texts(s.root, 1), "|22°|"), "the second tile of the same entity keeps HA's value");

    s.fx->transport->simulate_fragment(R"({"id":2,"type":"result","success":true,"result":{}})", true);
    snapshot(*s.fx, "climate.livingroom", "heat", 21.3, 23.5);
    expect(contains(texts(s.root, 0), "|23,5°|") && contains(texts(s.root, 1), "|23,5°|"),
           "HA's update confirms the target on both tiles");
    s.session->shutdown();
}

void test_failed_command_rolls_back() {
    Setup s = make_setup();
    snapshot(*s.fx, "climate.livingroom", "heat", 21.3, 22.0);
    press(s.root, 0, 0);
    let_send_delay_pass();
    expect(contains(texts(s.root, 0), "|21,5°|"), "pending target shown");
    s.fx->transport->simulate_fragment(
        R"({"id":2,"type":"result","success":false,"error":{"code":"x","message":"nope"}})", true);
    expect(contains(texts(s.root, 0), "|22°|"), "a failed command falls back to HA's value");
    s.session->shutdown();
}

void test_heat_cool_moves_the_closer_bound() {
    Setup s = make_setup("climate.office");
    heat_cool_snapshot(*s.fx, "climate.office", 20.5, 21.0, 24.0);
    expect(contains(texts(s.root, 0), "|21°|Kühlen ab 24°|"), "room near the lower bound: it is the big number");
    press(s.root, 0, 1);
    let_send_delay_pass();
    const std::string& sent = s.fx->transport->sent_messages.back();
    expect(contains(sent, "\"target_temp_low\":21.5") && contains(sent, "\"target_temp_high\":24"),
           "+ raises the lower bound and keeps the upper one: " + sent);
    s.session->shutdown();
}

void test_off_ignores_presses() {
    Setup s = make_setup();
    snapshot(*s.fx, "climate.livingroom", "off", 21.3, 22.0);
    const size_t before = s.fx->transport->sent_messages.size();
    press(s.root, 0, 1);
    let_send_delay_pass();
    expect(s.fx->transport->sent_messages.size() == before && contains(texts(s.root, 0), "|Aus|"),
           "a thermostat that is off shows Aus and sends nothing");
    s.session->shutdown();
}

void test_shutdown_during_in_flight_command_is_crash_free() {
    Setup s = make_setup();
    snapshot(*s.fx, "climate.livingroom", "heat", 21.3, 22.0);
    press(s.root, 0, 1);
    s.session->shutdown(); // dashboard switch while a send is still scheduled
    s.session.reset();
    let_send_delay_pass();
    expect(count_occurrences(s.fx->transport->sent_messages, "set_temperature") == 0,
           "a send scheduled by a torn-down tile never fires");
    expect(true, "tearing a tile down with a scheduled send does not crash");
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_two_widgets_share_subscription_and_render_initial_state();
    test_presses_are_collected_into_one_command();
    test_failed_command_rolls_back();
    test_heat_cool_moves_the_closer_bound();
    test_off_ignores_presses();
    test_shutdown_during_in_flight_command_is_crash_free();

    if (g_failures == 0) {
        std::printf("All climate tile end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
