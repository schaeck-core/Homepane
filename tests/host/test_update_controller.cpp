// Unit tests for the firmware update flow (Track U, Phase U1): manifest
// parsing, version comparison, URL resolution and UpdateController's state
// machine. Fake adapter/runner, no network, no flash, no LVGL.

#include "app/cpp/application/UpdateController.h"
#include "app/cpp/platform/PortableDefaults.h"
#include "app/cpp/update/UpdateManifest.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using app::application::UpdateController;
using app::application::UpdateState;
using app::update::compare_versions;
using app::update::parse_update_manifest;
using app::update::resolve_url;
using app::update::UpdateManifest;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

const std::string kSha(64, 'a');

std::string manifest_json(const std::string& version, const std::string& min_supported = "0.1.0")
{
    return R"({"version":")" + version + R"(","min_supported_version":")" + min_supported +
           R"(","path":"firmware/homepane.bin","sha256":")" + kSha +
           R"(","coprocessor":{"version":"2.12.3-sb.1","path":"firmware/c6fw.bin","sha256":")" + kSha + R"("}})";
}

class FakeClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override { return now_; }
    uint64_t now_ = 1000;
};

class ManualRunner final : public app::net::IBackgroundRunner {
public:
    void run(std::function<void()> job) override { jobs.push_back(std::move(job)); }
    void run_all()
    {
        auto pending = std::move(jobs);
        jobs.clear();
        for (auto& j : pending) j();
    }
    std::vector<std::function<void()>> jobs;
};

class FakeAdapter final : public app::update::IUpdateAdapter {
public:
    bool fetch_verified_manifest(const std::string& url, std::string& json, std::string& error) override
    {
        ++fetches;
        last_url = url;
        if (!signature_ok) {
            error = "signature invalid";
            return false;
        }
        json = manifest;
        return true;
    }
    bool install(const UpdateManifest& m, const std::string&, const Progress& progress, std::string& error) override
    {
        installed_manifest = m;
        progress(50);
        progress(50);
        progress(100);
        if (!install_ok) error = "sha256 mismatch";
        return install_ok;
    }
    std::string installed_version() const override { return installed; }
    std::string coprocessor_version() const override { return "2.12.3-sb.1"; }
    void restart() override { ++restarts; }

    std::string manifest;
    std::string installed = "0.1.0";
    std::string last_url;
    UpdateManifest installed_manifest;
    bool signature_ok = true;
    bool install_ok = true;
    int fetches = 0;
    int restarts = 0;
};

struct Fixture {
    FakeClock clock;
    FakeAdapter adapter;
    ManualRunner runner;
    app::platform::SynchronousTaskRunner ui;
    std::string url = "https://flasher.lan/firmware-manifest.json";
    bool auto_check = true;
    std::vector<UpdateState> states;
    UpdateController controller{adapter, runner, ui, clock, [this] { return url; }, [this] { return auto_check; }};
    Fixture()
    {
        controller.set_listener([this](const app::application::UpdateStatus& s) { states.push_back(s.state); });
    }
};

void test_versions_and_urls()
{
    expect(compare_versions("0.1.0", "0.1.0") == 0, "equal versions compare equal");
    expect(compare_versions("0.1.0", "0.1.0.1") < 0, "a CI build number makes a version newer");
    expect(compare_versions("0.2.0", "0.1.9.99") > 0, "minor beats build number");
    expect(compare_versions("2.12.3-sb.1", "2.12.3") == 0, "a -suffix is ignored for app versions");
    expect(resolve_url("https://h/a/firmware-manifest.json", "firmware/x.bin") == "https://h/a/firmware/x.bin",
           "relative paths resolve next to the manifest");
    expect(resolve_url("https://h/firmware-manifest.json?x=1", "/firmware/x.bin") == "https://h/firmware/x.bin",
           "query strings are dropped and a leading slash stays below the manifest directory");
    expect(resolve_url("https://h/m.json", "https://cdn/x.bin") == "https://cdn/x.bin", "absolute URLs pass through");
}

void test_manifest_parsing()
{
    UpdateManifest m;
    std::string err;
    expect(parse_update_manifest(manifest_json("0.1.0.7"), m, err) && m.version == "0.1.0.7" && m.has_coprocessor &&
               m.coprocessor_path == "firmware/c6fw.bin",
           "a CI manifest parses, including the coprocessor entry");
    expect(!parse_update_manifest(R"({"version":"0.2.0","path":"x.bin","sha256":"nothex"})", m, err),
           "a manifest with an invalid sha256 is rejected");
    expect(!parse_update_manifest("not json", m, err), "garbage is rejected");
}

void test_check_finds_newer_version_and_installs()
{
    Fixture fx;
    fx.adapter.manifest = manifest_json("0.1.0.12");
    expect(fx.controller.check(), "check() starts");
    expect(fx.controller.status().state == UpdateState::Checking, "state is Checking while the fetch runs");
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::Available &&
               fx.controller.status().available_version == "0.1.0.12",
           "a newer version is reported as Available");
    expect(fx.adapter.last_url == fx.url, "the configured URL is fetched");

    expect(fx.controller.install(), "install() starts when an update is available");
    fx.runner.run_all();
    expect(fx.adapter.installed_manifest.version == "0.1.0.12", "the checked manifest is installed");
    expect(fx.controller.status().state == UpdateState::Restarting, "a successful install schedules a restart");
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.adapter.restarts == 0, "no restart before the delay");
    fx.clock.now_ += UpdateController::kRestartDelayMs;
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.adapter.restarts == 1, "restart after the delay");
}

void test_coprocessor_updates_off()
{
    Fixture fx;
    fx.adapter.manifest = manifest_json("0.2.0");
    bool c6 = false;
    fx.controller.set_coprocessor_updates([&c6] { return c6; });
    fx.controller.check();
    fx.runner.run_all();
    fx.controller.install();
    fx.runner.run_all();
    expect(fx.adapter.installed_manifest.version == "0.2.0" && !fx.adapter.installed_manifest.has_coprocessor,
           "with co-processor updates off the C6 image is left out");

    Fixture on;
    on.adapter.manifest = manifest_json("0.2.0");
    on.controller.set_coprocessor_updates([] { return true; });
    on.controller.check();
    on.runner.run_all();
    on.controller.install();
    on.runner.run_all();
    expect(on.adapter.installed_manifest.has_coprocessor, "with co-processor updates on the C6 image is installed");
}

void test_up_to_date_and_downgrade()
{
    Fixture fx;
    fx.adapter.installed = "0.1.0.12";
    fx.adapter.manifest = manifest_json("0.1.0.12");
    fx.controller.check();
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::UpToDate, "same version: up to date");

    fx.adapter.manifest = manifest_json("0.1.0.3");
    fx.controller.check();
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::UpToDate, "an older manifest never offers a downgrade");
    expect(!fx.controller.install(), "install() is refused when nothing is available");
}

void test_bad_signature_and_failed_install()
{
    Fixture fx;
    fx.adapter.manifest = manifest_json("0.2.0");
    fx.adapter.signature_ok = false;
    fx.controller.check();
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::Error, "a manifest failing verification is an error");

    fx.adapter.signature_ok = true;
    fx.adapter.install_ok = false;
    fx.controller.check();
    fx.runner.run_all();
    fx.controller.install();
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::Error, "a failed install ends in Error");
    fx.clock.now_ += 60000;
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.adapter.restarts == 0, "no restart after a failed install");
}

void test_min_supported_version()
{
    Fixture fx;
    fx.adapter.installed = "0.1.0";
    fx.adapter.manifest = manifest_json("0.3.0", "0.2.0");
    fx.controller.check();
    fx.runner.run_all();
    expect(fx.controller.status().state == UpdateState::Error && !fx.controller.install(),
           "a version requiring a newer installed base is not installable");
}

void test_auto_check_schedule()
{
    Fixture fx;
    fx.adapter.manifest = manifest_json("0.1.0");
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.runner.jobs.empty(), "no check right at boot");
    fx.clock.now_ += UpdateController::kFirstAutoCheckDelayMs;
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.runner.jobs.size() == 1, "first automatic check a few minutes after boot");
    fx.runner.run_all();
    fx.clock.now_ += 1000;
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.runner.jobs.empty(), "no second check before the interval");
    fx.clock.now_ += UpdateController::kAutoCheckIntervalMs;
    fx.controller.poll(fx.clock.now_ms());
    expect(fx.runner.jobs.size() == 1, "checked again after the interval");

    Fixture off;
    off.auto_check = false;
    off.controller.poll(off.clock.now_ms());
    off.clock.now_ += UpdateController::kFirstAutoCheckDelayMs;
    off.controller.poll(off.clock.now_ms());
    expect(off.runner.jobs.empty(), "automatic checks can be disabled");

    Fixture nourl;
    nourl.url.clear();
    expect(!nourl.controller.check() && nourl.controller.status().state == UpdateState::Error,
           "a manual check without a configured URL reports an error");
}

} // namespace

int main()
{
    test_versions_and_urls();
    test_manifest_parsing();
    test_check_finds_newer_version_and_installs();
    test_coprocessor_updates_off();
    test_up_to_date_and_downgrade();
    test_bad_signature_and_failed_install();
    test_min_supported_version();
    test_auto_check_schedule();

    if (g_failures == 0) {
        std::printf("All update tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
