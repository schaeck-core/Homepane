// Integration test for DashboardController — the live composition root
// wired up for the cutover (CPP_MIGRATION_PLAN.md's "gebündelter
// Cutover nach Phase 9"). Drives the full real stack — FakeWebSocketTransport
// -> WebSocketConnection -> HaClient -> EntityStore/Coordinators/
// WidgetFactory -> DashboardSession -> real headless LVGL — through the
// SAME entry point app_facade.cpp uses live, so this is the first place
// the actual connect -> auth -> fetch lovelace/config -> parse -> build
// dashboard sequence is exercised end to end, using the real captured
// tests/fixtures/ha/lovelace_config_result.json.

#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/application/DashboardController.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace app::application;
using namespace app::home_assistant;
using app::core::IMonotonicClock;
using app::core::IWallClock;
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

class FakeWallClock final : public IWallClock {
public:
    double now_epoch_s() const override { return 1'700'000'000.0; }
};

void noop_flush_cb(lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); }

lv_obj_t* make_headless_screen() {
    lv_display_t* disp = lv_display_create(320, 240);
    static uint8_t buf[320 * 10 * 4];
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

std::string load_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The fixture is a full {"id":1,"type":"result","success":true,"result":{...}}
// message (captured from a real HA server) — extract just the "result"
// object's raw text, matching what HaClient::ResultMessage::result_json
// actually carries (the id/type/success envelope is HaClient's job, not
// something DashboardController or DashboardParser ever sees).
std::string extract_result_object(const std::string& full_message_json) {
    cJSON* root = cJSON_Parse(full_message_json.c_str());
    if (!root) return {};
    std::string out;
    cJSON* result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (result) {
        char* txt = cJSON_PrintUnformatted(result);
        if (txt) {
            out = txt;
            cJSON_free(txt);
        }
    }
    cJSON_Delete(root);
    return out;
}

struct Fixture {
    FakeClock clock;
    FakeWallClock wall_clock;
    FakeWebSocketTransport* transport;
    std::unique_ptr<DashboardController> controller;
};

std::unique_ptr<Fixture> make_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    fx->controller = std::make_unique<DashboardController>(fx->clock, fx->wall_clock, nullptr, std::move(transport));
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
}

void reply_lovelace_config(Fixture& fx, int id, const std::string& result_json) {
    fx.transport->simulate_fragment(
        R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":true,"result":)" + result_json + "}",
        true);
}

// The last lovelace/config request sent, and its id.
std::string last_lovelace_request(const Fixture& fx) {
    for (auto it = fx.transport->sent_messages.rbegin(); it != fx.transport->sent_messages.rend(); ++it) {
        if (it->find("\"type\":\"lovelace/config\"") != std::string::npos) return *it;
    }
    return "";
}

int request_id(const std::string& msg) {
    const size_t p = msg.find("\"id\":");
    return p == std::string::npos ? 0 : std::atoi(msg.c_str() + p + 5);
}

void test_connects_and_authenticates_with_the_given_url_and_token() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "the-real-token", "main",
        [] {}, [] {});

    expect(fx->transport->connect_calls.size() == 1, "render() connects the transport exactly once");
    expect(fx->transport->connect_calls[0] == "ws://ha.local:8123/api/websocket",
           "the transport is connected to the exact ws_url passed to render()");

    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);

    expect(count_occurrences(fx->transport->sent_messages, "\"access_token\":\"the-real-token\"") == 1,
           "the auth frame carries the exact access_token passed to render()");
}

void test_ready_fetches_lovelace_config_for_the_given_dashboard() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    fx->controller->render(screen, "ws://ha.local:8123/api/websocket", "token", "kitchen", [] {}, [] {});
    reach_ready(*fx);

    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"lovelace/config\"") == 1,
           "reaching Ready triggers exactly one lovelace/config request");
    expect(last_lovelace_request(*fx).find("\"url_path\":\"kitchen\"") != std::string::npos,
           "the request targets the exact dashboard_url_path passed to render()");
    expect(last_lovelace_request(*fx).find("\"force\":true") != std::string::npos,
           "the request matches the legacy callback_connected()'s force:true shape");
}

void test_real_captured_dashboard_builds_a_real_session_and_fires_on_ready() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    bool ready_fired = false;
    bool error_fired = false;
    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "token", "main", [&] { ready_fired = true; },
        [&] { error_fired = true; });
    reach_ready(*fx);

    std::string full = load_file("tests/fixtures/ha/lovelace_config_result.json");
    std::string result_json = extract_result_object(full);
    expect(!result_json.empty(), "the real captured dashboard fixture loads and its result object extracts");

    reply_lovelace_config(*fx, 1, result_json);

    expect(ready_fired, "on_ready fires once the real dashboard has been parsed and built");
    expect(!error_fired, "on_error does not fire on a successful build");

    // The real dashboard has 2 views -> 2 direct children of the session
    // root, which is itself the one child DashboardController attaches
    // under `screen` (see DashboardSession::create()'s tree shape).
    expect(lv_obj_get_child_count(screen) == 1, "exactly one real DashboardSession root is attached under screen");
    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(lv_obj_get_child_count(session_root) == 2,
           "the real dashboard's 2 views become 2 real paged children, same as DashboardSession's own test");
}

void test_auth_failure_before_first_render_calls_on_error() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    bool error_fired = false;
    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "bad-token", "main", [] {}, [&] { error_fired = true; });
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_invalid","message":"Invalid token"})", true);

    expect(error_fired, "auth_invalid before any dashboard has ever loaded fires on_error");
}

void test_failed_lovelace_config_calls_on_error() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    bool error_fired = false;
    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "token", "does-not-exist", [] {}, [&] { error_fired = true; });
    reach_ready(*fx);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"result","success":false,"error":{"code":"not_found","message":"nope"}})", true);

    expect(error_fired, "a failed lovelace/config Result before any dashboard has ever loaded fires on_error");
}

void test_reconnect_after_dashboard_built_does_not_refire_error_or_rebuild() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    int ready_calls = 0;
    bool error_fired = false;
    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "token", "main", [&] { ++ready_calls; },
        [&] { error_fired = true; });
    reach_ready(*fx);

    std::string full = load_file("tests/fixtures/ha/lovelace_config_result.json");
    reply_lovelace_config(*fx, 1, extract_result_object(full));
    expect(ready_calls == 1, "the dashboard builds once on the first successful fetch");

    // A later disconnect (e.g. a network blip) must not tear down the
    // already-showing dashboard or fire the error path — that's what
    // EntityStore's own `stale` state is for (§5/§7.5).
    fx->transport->simulate_closed("network blip");
    fx->controller->poll(fx->clock.now_ms());

    expect(!error_fired, "a disconnect after the dashboard is already showing does not fire on_error");
    expect(lv_obj_get_child_count(screen) == 1,
           "the already-built dashboard's LVGL tree is untouched by a mere disconnect");
}

void test_disconnect_before_any_render_is_a_safe_noop() {
    auto fx = make_fixture();
    // No render() call at all yet — the settings screen's "Reset
    // Connection" button (CPP_MIGRATION_PLAN.md Phase 10's "Token"
    // consumer) can legitimately fire this before the dashboard has ever
    // connected (e.g. the very first boot's Setup flow never reached the
    // dashboard).
    fx->controller->disconnect();
    expect(true, "disconnect() before any render() call does not crash");
}

void test_disconnect_stops_the_live_connection() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    fx->controller->render(
        screen, "ws://ha.local:8123/api/websocket", "token", "main", [] {}, [] {});
    reach_ready(*fx);

    size_t sent_before = fx->transport->sent_messages.size();
    fx->controller->disconnect();

    expect(fx->transport->close_calls >= 1,
           "disconnect() actually closes the underlying transport, not just a local flag");

    // Matches HaClient::stop()'s own contract: no live in-place
    // reconnect attempt follows on its own — the caller (the settings
    // screen, in production) tells the user to restart the app instead.
    fx->controller->poll(fx->clock.now_ms());
    expect(fx->transport->sent_messages.size() == sent_before,
           "disconnect() does not trigger a fresh auth handshake or any other message on its own");
}

} // namespace

void test_dashboard_change_in_ha_reloads_it() {
    auto fx = make_fixture();
    lv_obj_t* screen = make_headless_screen();

    int ready_calls = 0;
    fx->controller->render(screen, "ws://ha.local:8123/api/websocket", "token", "main", [&] { ++ready_calls; },
                           [] {});
    reach_ready(*fx);
    expect(count_occurrences(fx->transport->sent_messages, "\"event_type\":\"lovelace_updated\"") == 1,
           "Ready subscribes to dashboard changes");
    const std::string config = extract_result_object(load_file("tests/fixtures/ha/lovelace_config_result.json"));
    reply_lovelace_config(*fx, 1, config); // id 1: lovelace/config, id 2: the subscription

    fx->transport->simulate_fragment(
        R"({"id":2,"type":"event","event":{"event_type":"lovelace_updated","data":{"url_path":"other","mode":"storage"}}})",
        true);
    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"lovelace/config\"") == 1,
           "a change to another dashboard is ignored");

    fx->transport->simulate_fragment(
        R"({"id":2,"type":"event","event":{"event_type":"lovelace_updated","data":{"url_path":"main","mode":"storage"}}})",
        true);
    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"lovelace/config\"") == 2,
           "a change to the shown dashboard fetches its config again");
    reply_lovelace_config(*fx, request_id(last_lovelace_request(*fx)), config);
    expect(ready_calls == 2 && lv_obj_get_child_count(screen) == 1,
           "the dashboard is rebuilt in place, one session root as before");

    fx->controller->reload();
    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"lovelace/config\"") == 3,
           "reload() fetches the config again");
}

int main() {
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_connects_and_authenticates_with_the_given_url_and_token();
    test_ready_fetches_lovelace_config_for_the_given_dashboard();
    test_real_captured_dashboard_builds_a_real_session_and_fires_on_ready();
    test_auth_failure_before_first_render_calls_on_error();
    test_failed_lovelace_config_calls_on_error();
    test_reconnect_after_dashboard_built_does_not_refire_error_or_rebuild();
    test_disconnect_before_any_render_is_a_safe_noop();
    test_disconnect_stops_the_live_connection();
    test_dashboard_change_in_ha_reloads_it();

    if (g_failures == 0) {
        std::printf("All DashboardController integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
