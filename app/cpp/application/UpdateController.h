#ifndef APP_CPP_APPLICATION_UPDATECONTROLLER_H
#define APP_CPP_APPLICATION_UPDATECONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../core/ITaskRunner.h"
#include "../net/IWifiAdapter.h"
#include "../update/IUpdateAdapter.h"
#include "../update/UpdateManifest.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace app::application {

enum class UpdateState {
    Idle,        // nothing checked yet
    Checking,
    UpToDate,
    Available,   // a newer, installable version is in `available_version`
    Installing,
    Restarting,  // installed; restart scheduled
    Error,       // `message` says why
};

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    std::string installed_version;
    std::string coprocessor_version;
    std::string available_version;
    std::string message;
    int progress = 0; // 0-100 while Installing
};

// Firmware update flow (Track U, Phase U1 — docs/ARCHITECTURE.md §13):
// fetch the signed manifest from `manifest_url`, compare versions, install
// into the inactive slot, restart. Lives on the UI thread; all callbacks
// fire there. Network and flash work run through IBackgroundRunner.
class UpdateController {
public:
    using Listener = std::function<void(const UpdateStatus&)>;
    using UrlProvider = std::function<std::string()>;
    using AutoCheckEnabled = std::function<bool()>;

    static constexpr uint64_t kFirstAutoCheckDelayMs = 5ull * 60 * 1000;  // after boot
    static constexpr uint64_t kAutoCheckIntervalMs = 24ull * 60 * 60 * 1000;
    static constexpr uint64_t kRestartDelayMs = 2000;

    UpdateController(app::update::IUpdateAdapter& adapter, app::net::IBackgroundRunner& background,
                     app::core::ITaskRunner& ui, app::core::IMonotonicClock& clock, UrlProvider manifest_url,
                     AutoCheckEnabled auto_check, app::core::ILogger* logger = nullptr);

    // Called on every status change (UI thread).
    void set_listener(Listener listener) { listener_ = std::move(listener); }
    // False: install() leaves out the co-processor image, so the C6 keeps
    // its firmware. Unset = on.
    void set_coprocessor_updates(std::function<bool()> enabled) { coprocessor_updates_ = std::move(enabled); }

    // Starts a check. False if a check/install is running or no URL is set.
    bool check();
    // Installs the version found by the last check. False unless Available.
    bool install();

    const UpdateStatus& status() const { return status_; }
    void poll(uint64_t now_ms);

private:
    struct CheckResult;
    struct InstallResult;
    static void deliver_check(void* ctx);
    static void deliver_install(void* ctx);
    static void deliver_progress(void* ctx);

    void set_state(UpdateState state, const std::string& message = "");
    void refresh_versions();

    app::update::IUpdateAdapter& adapter_;
    app::net::IBackgroundRunner& background_;
    app::core::ITaskRunner& ui_;
    app::core::IMonotonicClock& clock_;
    UrlProvider manifest_url_;
    AutoCheckEnabled auto_check_;
    std::function<bool()> coprocessor_updates_;
    app::core::ILogger* logger_;
    Listener listener_;

    UpdateStatus status_;
    app::update::UpdateManifest manifest_;
    std::string checked_url_;
    uint64_t next_auto_check_ms_ = 0;
    uint64_t restart_at_ms_ = 0;
    bool started_ = false;
    std::atomic<int> progress_pending_{0}; // written by the install task, read on the UI thread
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_UPDATECONTROLLER_H
