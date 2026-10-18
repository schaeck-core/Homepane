// Unit tests for WifiController (settings "WiFi" tab / setup screen flow).
// Fake adapter + fake background runner + synchronous task runner: no
// threads, no LVGL, no hardware.

#include "app/cpp/application/WifiController.h"
#include "app/cpp/platform/PortableDefaults.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using app::application::WifiController;
using app::net::WifiLinkState;
using app::net::WifiLinkStatus;
using app::net::WifiNetwork;

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

class FakeClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override { return now_; }
    uint64_t now_ = 1000;
};

class FakeAdapter final : public app::net::IWifiAdapter {
public:
    std::vector<WifiNetwork> scan(size_t) override { ++scan_calls; return scan_result; }
    bool connect(const std::string& ssid, const std::string& password) override {
        connect_calls.push_back(ssid + "/" + password);
        if (!available) return false;
        st = {WifiLinkState::Connecting, ssid, ""};
        return true;
    }
    WifiLinkStatus status() const override { return st; }

    std::vector<WifiNetwork> scan_result;
    int scan_calls = 0;
    std::vector<std::string> connect_calls;
    bool available = true;
    WifiLinkStatus st;
};

// Queues jobs; the test decides when the "background thread" runs them.
class ManualRunner final : public app::net::IBackgroundRunner {
public:
    void run(std::function<void()> job) override { jobs.push_back(std::move(job)); }
    void run_all() {
        auto pending = std::move(jobs);
        jobs.clear();
        for (auto& j : pending) j();
    }
    std::vector<std::function<void()>> jobs;
};

struct Fixture {
    FakeClock clock;
    FakeAdapter adapter;
    ManualRunner runner;
    app::platform::SynchronousTaskRunner ui;
    std::vector<std::string> persisted;
    WifiController controller{adapter, runner, ui, clock,
                              [this](const std::string& s, const std::string& p) { persisted.push_back(s + "/" + p); }};
};

void test_normalize_drops_hidden_merges_duplicates_and_sorts() {
    auto out = WifiController::normalize({
        {"", -40, true},          // hidden
        {"Home", -70, true},
        {"Guest", -60, false},
        {"Home", -50, true},      // second access point of the same network, stronger
    });
    expect(out.size() == 2, "hidden SSIDs are dropped and duplicate SSIDs merged");
    expect(out.size() == 2 && out[0].ssid == "Home" && out[0].rssi == -50,
           "a duplicate SSID keeps its strongest access point and the list is sorted by signal");
    expect(out.size() == 2 && out[1].ssid == "Guest", "weaker networks come after stronger ones");
}

void test_scan_runs_in_background_and_reports_on_ui_thread() {
    Fixture fx;
    fx.adapter.scan_result = {{"A", -80, true}, {"B", -40, false}};
    std::vector<WifiNetwork> got;
    bool called = false;

    expect(fx.controller.scan([&](const std::vector<WifiNetwork>& n) { got = n; called = true; }),
           "scan() starts a scan");
    expect(fx.adapter.scan_calls == 0 && !called, "the (blocking) adapter scan does not run on the calling thread");
    expect(!fx.controller.scan([](const std::vector<WifiNetwork>&) {}), "a second scan while one is running is refused");

    fx.runner.run_all();
    expect(called && got.size() == 2 && got[0].ssid == "B", "results arrive normalized (sorted by signal)");
    expect(!fx.controller.scanning(), "scanning() is false again after the results arrived");
}

void test_connect_success_persists_credentials() {
    Fixture fx;
    bool done = false, ok = false;
    fx.controller.connect("Home", "secret", [&](bool r, const std::string&) { done = true; ok = r; });
    expect(fx.adapter.connect_calls.size() == 1 && fx.adapter.connect_calls[0] == "Home/secret",
           "connect() hands ssid and password to the adapter");

    fx.controller.poll(fx.clock.now_ms());
    expect(!done, "still connecting: no callback yet");

    fx.adapter.st = {WifiLinkState::Connected, "Home", "192.0.2.105"};
    fx.controller.poll(fx.clock.now_ms());
    expect(done && ok, "callback reports success once connected to the requested network");
    expect(fx.persisted.size() == 1 && fx.persisted[0] == "Home/secret", "credentials are saved only on success");
}

void test_wrong_password_reports_failure_and_saves_nothing() {
    Fixture fx;
    bool done = false, ok = true;
    std::string msg;
    fx.controller.connect("Home", "wrong", [&](bool r, const std::string& m) { done = true; ok = r; msg = m; });
    fx.adapter.st = {WifiLinkState::Failed, "Home", ""};
    fx.controller.poll(fx.clock.now_ms());
    expect(done && !ok && !msg.empty(), "a failed attempt reports failure with a message");
    expect(fx.persisted.empty(), "nothing is saved for a failed attempt");
}

void test_stale_connection_to_other_network_is_not_success() {
    Fixture fx;
    bool done = false;
    fx.controller.connect("New", "pw", [&](bool, const std::string&) { done = true; });
    // The previous network still reads "connected" until it is torn down.
    fx.adapter.st = {WifiLinkState::Connected, "Old", "192.0.2.109"};
    fx.controller.poll(fx.clock.now_ms());
    expect(!done && fx.persisted.empty(),
           "still being connected to the old network is neither success nor saved");
}

void test_timeout() {
    Fixture fx;
    bool done = false, ok = true;
    fx.controller.connect("Home", "pw", [&](bool r, const std::string&) { done = true; ok = r; });
    fx.clock.now_ += WifiController::kConnectTimeoutMs - 1;
    fx.controller.poll(fx.clock.now_ms());
    expect(!done, "no timeout before the deadline");
    fx.clock.now_ += 2;
    fx.controller.poll(fx.clock.now_ms());
    expect(done && !ok, "a hanging attempt times out with ok=false");
}

void test_new_connect_supersedes_pending_one() {
    Fixture fx;
    int first_calls = 0;
    bool first_ok = true;
    fx.controller.connect("A", "1", [&](bool r, const std::string&) { ++first_calls; first_ok = r; });
    bool second_ok = false;
    fx.controller.connect("B", "2", [&](bool r, const std::string&) { second_ok = r; });
    expect(first_calls == 1 && !first_ok, "a still pending attempt is completed with ok=false when superseded");
    fx.adapter.st = {WifiLinkState::Connected, "B", "192.0.2.102"};
    fx.controller.poll(fx.clock.now_ms());
    expect(second_ok && first_calls == 1, "the new attempt completes normally, the old callback never fires again");
}

void test_unavailable_wifi_fails_immediately() {
    Fixture fx;
    fx.adapter.available = false;
    bool done = false, ok = true;
    fx.controller.connect("Home", "pw", [&](bool r, const std::string&) { done = true; ok = r; });
    expect(done && !ok && !fx.controller.connecting(), "no WiFi hardware: fails immediately");
}

} // namespace

int main() {
    test_normalize_drops_hidden_merges_duplicates_and_sorts();
    test_scan_runs_in_background_and_reports_on_ui_thread();
    test_connect_success_persists_credentials();
    test_wrong_password_reports_failure_and_saves_nothing();
    test_stale_connection_to_other_network_is_not_success();
    test_timeout();
    test_new_connect_supersedes_pending_one();
    test_unavailable_wifi_fails_immediately();

    if (g_failures == 0) {
        std::printf("All WifiController tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
