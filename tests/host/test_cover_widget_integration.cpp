// End-to-end integration test of the cover tile (CoverTile, design guide):
// open/close commands, the reported position drawn as the shade of a
// window, generation-safe command cancellation — through the full real
// stack down to real LVGL objects and simulated taps. Only the network
// socket is faked.
//
// A cover's movement is reported by HA itself (state "opening"/"closing"),
// so the tile does not predict a position; it shows command failures and
// timeouts as a toast and otherwise waits for the real state.

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

// CoverTile's LVGL tree (CoverTile::build()):
//   tile -> [0]=body -> [0]=name, [1]=window -> [0]=pane -> [0..1]=cross bars, [2]=shade
//                                             [1]=sill
//           [1]=button row -> [0]=up, [1]=down
// The shade's share of the pane is the observable position (100 = closed).
lv_obj_t* widget_root(lv_obj_t* session_root, uint32_t widget_index) {
    return dashboard_test::widget_root_at(session_root, widget_index);
}

int shade_percent(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* root = widget_root(session_root, widget_index);
    if (!root) return -1;
    lv_obj_update_layout(root);
    lv_obj_t* pane = lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(root, 0), 1), 0);
    lv_obj_t* shade = lv_obj_get_child(pane, 2);
    const int32_t inner = lv_obj_get_content_height(pane);
    return inner > 0 ? static_cast<int>(lv_obj_get_height(shade) * 100 / inner + 0.5) : -1;
}

// A failed command shows a toast on the top layer (ui_kit).
bool near(int a, int b) { return a >= b - 1 && a <= b + 1; }

bool failure_toast_visible() {
    lv_obj_t* top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); ++i) {
        lv_obj_t* toast = lv_obj_get_child(top, static_cast<int32_t>(i));
        lv_obj_t* label = lv_obj_get_child(toast, 0);
        if (label && std::string(lv_label_get_text(label)).find("fehlgeschlagen") != std::string::npos) return true;
    }
    return false;
}

void clear_toasts() { lv_obj_clean(lv_layer_top()); }

enum class Button { Open, Close };

void tap(lv_obj_t* session_root, uint32_t widget_index, Button button) {
    lv_obj_t* root = widget_root(session_root, widget_index);
    lv_obj_t* row = lv_obj_get_child(root, 1);
    lv_obj_send_event(lv_obj_get_child(row, button == Button::Open ? 0 : 1), LV_EVENT_CLICKED, nullptr);
}

DashboardModel make_two_cover_tiles_model(const std::string& entity_id) {
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

void snapshot(Fixture& fx, const std::string& entity_id, const std::string& state, int position) {
    fx.transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{")" + entity_id + R"(":{"s":")" + state +
            R"(","a":{"friendly_name":"Living Room Window","current_position":)" + std::to_string(position) +
            R"(},"lc":1000.0}}}})",
        true);
}

void test_two_widgets_share_subscription_and_open_sends_correct_command() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);

    expect(session.widget_count() == 2, "two tile cards for the same cover entity yield two real widget instances");
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1,
           "two cover tiles observing the same entity still result in exactly one subscribe_entities call");

    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "closed", 0);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(near(shade_percent(session_root, 0), 100), "position 0 (closed): the shade covers the whole window, before any tap");

    tap(session_root, 0, Button::Open);

    expect(count_occurrences(fx->transport->sent_messages, "\"domain\":\"cover\"") == 1,
           "tapping open sends exactly one call_service message");
    expect(count_occurrences(fx->transport->sent_messages, "\"service\":\"open_cover\"") == 1,
           "the open button calls open_cover");
    expect(fx->transport->sent_messages.back().find("\"entity_id\":\"cover.window\"") != std::string::npos,
           "the service call targets the correct entity_id");
    expect(near(shade_percent(session_root, 0), 100),
           "tapping does not predict a position: the shade only moves once a real event arrives");

    session.shutdown();
}

void test_close_sends_correct_command() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "open", 100);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0, Button::Close);
    expect(count_occurrences(fx->transport->sent_messages, "\"service\":\"close_cover\"") == 1,
           "the close button calls close_cover");

    session.shutdown();
}

void test_real_events_move_the_shade() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "open", 70);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(near(shade_percent(session_root, 0), 30), "initial snapshot at position 70: the shade covers 30 %");

    // Rapid incremental deltas, mirroring a real captured HA sequence
    // (tests/fixtures/ha/cover_delta_sequence.ndjson): state flips to
    // "opening" first, then position-only deltas follow.
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"cover.window":{"+":{"s":"opening","lc":1001.0}}}}})", true);
    expect(near(shade_percent(session_root, 0), 30),
           "a state-only delta keeps the last known position");

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"cover.window":{"+":{"lu":1002.0,"a":{"current_position":90}}}}}})",
        true);
    expect(near(shade_percent(session_root, 0), 10), "a position-only delta to 90: the shade covers 10 %");

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"cover.window":{"+":{"s":"open","lc":1003.0,"a":{"current_position":100}}}}}})",
        true);
    expect(near(shade_percent(session_root, 0), 0), "the final delta to fully open (100): no shade");

    session.shutdown();
}

void test_failed_command_shows_a_toast() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "closed", 0);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    clear_toasts();
    expect(!failure_toast_visible(), "no failure toast before any command");
    tap(session_root, 0, Button::Open);

    fx->transport->simulate_fragment(
        R"({"id":2,"type":"result","success":false,"error":{"code":"unknown_error","message":"nope"}})", true);
    expect(failure_toast_visible(),
           "a failed call_service Result shows a toast, not silent nothing");
    expect(near(shade_percent(session_root, 0), 100), "the failure doesn't change the (still closed) position");

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"c":{"cover.window":{"+":{"s":"open","lc":1002.0,"a":{"current_position":20}}}}}})",
        true);
    expect(near(shade_percent(session_root, 0), 80), "the next real update shows the new position (position 20: 80 % shade)");

    session.shutdown();
}

void test_timeout_shows_a_toast() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "closed", 0);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    clear_toasts();
    tap(session_root, 0, Button::Close);

    fx->clock.advance(11000); // past HaClient's default 10s command timeout
    fx->client->poll(fx->clock.now_ms());

    expect(failure_toast_visible(),
           "a timed-out command shows the same toast as an explicit failure");

    session.shutdown();
}

void test_second_widget_unaffected_by_siblings_failed_command() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1),
                              fx->coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "closed", 0);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    clear_toasts();
    tap(session_root, 0, Button::Open); // only the FIRST widget is tapped

    fx->transport->simulate_fragment(
        R"({"id":2,"type":"result","success":false,"error":{"code":"unknown_error","message":"nope"}})", true);

    expect(failure_toast_visible(), "the failure is shown");
    expect(near(shade_percent(session_root, 1), 100),
           "the untapped second widget for the same entity keeps showing the real position");

    session.shutdown();
}

void test_shutdown_during_in_flight_command_is_crash_free() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_cover_tiles_model("cover.window");
    auto session = std::make_unique<DashboardSession>(std::move(model), fx->factory,
                                                        app::core::DashboardGeneration(1), fx->coordinator.get());
    session->create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    snapshot(*fx, "cover.window", "closed", 0);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    tap(session_root, 0, Button::Open); // starts an in-flight call_service, id=2

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

    test_two_widgets_share_subscription_and_open_sends_correct_command();
    test_close_sends_correct_command();
    test_real_events_move_the_shade();
    test_failed_command_shows_a_toast();
    test_timeout_shows_a_toast();
    test_second_widget_unaffected_by_siblings_failed_command();
    test_shutdown_during_in_flight_command_is_crash_free();

    if (g_failures == 0) {
        std::printf("All cover tile end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
