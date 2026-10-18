#include "UpdateController.h"

#include <atomic>
#include <utility>

namespace app::application {

using app::core::LogLevel;
using app::update::UpdateManifest;

struct UpdateController::CheckResult {
    UpdateController* self;
    std::string url;
    bool ok;
    std::string error;
    UpdateManifest manifest;
};

struct UpdateController::InstallResult {
    UpdateController* self;
    bool ok;
    std::string error;
};

UpdateController::UpdateController(app::update::IUpdateAdapter& adapter, app::net::IBackgroundRunner& background,
                                   app::core::ITaskRunner& ui, app::core::IMonotonicClock& clock,
                                   UrlProvider manifest_url, AutoCheckEnabled auto_check, app::core::ILogger* logger)
    : adapter_(adapter),
      background_(background),
      ui_(ui),
      clock_(clock),
      manifest_url_(std::move(manifest_url)),
      auto_check_(std::move(auto_check)),
      logger_(logger) {}

void UpdateController::refresh_versions()
{
    status_.installed_version = adapter_.installed_version();
    status_.coprocessor_version = adapter_.coprocessor_version();
}

void UpdateController::set_state(UpdateState state, const std::string& message)
{
    status_.state = state;
    status_.message = message;
    if (logger_ && !message.empty()) {
        logger_->log(state == UpdateState::Error ? LogLevel::Warn : LogLevel::Info, ("Update: " + message).c_str());
    }
    if (listener_) listener_(status_);
}

bool UpdateController::check()
{
    if (status_.state == UpdateState::Checking || status_.state == UpdateState::Installing ||
        status_.state == UpdateState::Restarting) {
        return false;
    }
    refresh_versions();
    std::string url = manifest_url_ ? manifest_url_() : "";
    if (url.empty()) {
        set_state(UpdateState::Error, "No update source configured.");
        return false;
    }
    set_state(UpdateState::Checking);

    background_.run([this, url] {
        auto* r = new CheckResult{this, url, false, "", {}};
        std::string json;
        if (adapter_.fetch_verified_manifest(url, json, r->error)) {
            r->ok = app::update::parse_update_manifest(json, r->manifest, r->error);
        }
        app::core::PostOptions options;
        options.critical = true; // must never be dropped: it ends the Checking state
        ui_.post(&UpdateController::deliver_check, r, options);
    });
    return true;
}

void UpdateController::deliver_check(void* ctx)
{
    auto* r = static_cast<CheckResult*>(ctx);
    UpdateController* self = r->self;
    if (!r->ok) {
        self->set_state(UpdateState::Error, r->error);
    } else {
        const UpdateManifest& m = r->manifest;
        const std::string& installed = self->status_.installed_version;
        if (app::update::compare_versions(m.version, installed) <= 0) {
            self->status_.available_version.clear();
            self->set_state(UpdateState::UpToDate, "Firmware is up to date.");
        } else if (!m.min_supported_version.empty() &&
                   app::update::compare_versions(installed, m.min_supported_version) < 0) {
            self->status_.available_version = m.version;
            self->set_state(UpdateState::Error, "Version " + m.version + " requires at least " +
                                                    m.min_supported_version + " to be installed first.");
        } else {
            self->manifest_ = m;
            self->checked_url_ = r->url;
            self->status_.available_version = m.version;
            self->set_state(UpdateState::Available, "Version " + m.version + " is available.");
        }
    }
    delete r;
}

bool UpdateController::install()
{
    if (status_.state != UpdateState::Available) return false;
    status_.progress = 0;
    set_state(UpdateState::Installing, "Installing " + manifest_.version + "...");

    UpdateManifest manifest = manifest_;
    if (coprocessor_updates_ && !coprocessor_updates_()) manifest.has_coprocessor = false;
    std::string url = checked_url_;
    background_.run([this, manifest, url] {
        auto* r = new InstallResult{this, false, ""};
        int last = -1;
        r->ok = adapter_.install(manifest, url,
                                 [this, &last](int percent) {
                                     if (percent == last) return;
                                     last = percent;
                                     progress_pending_.store(percent);
                                     // ctx is `this` (no allocation), so a dropped
                                     // post under load costs nothing.
                                     ui_.post(&UpdateController::deliver_progress, this);
                                 },
                                 r->error);
        app::core::PostOptions options;
        options.critical = true; // ends the Installing state
        ui_.post(&UpdateController::deliver_install, r, options);
    });
    return true;
}

void UpdateController::deliver_progress(void* ctx)
{
    auto* self = static_cast<UpdateController*>(ctx);
    if (self->status_.state != UpdateState::Installing) return;
    int p = self->progress_pending_.load();
    if (p == self->status_.progress) return;
    self->status_.progress = p;
    if (self->listener_) self->listener_(self->status_);
}

void UpdateController::deliver_install(void* ctx)
{
    auto* r = static_cast<InstallResult*>(ctx);
    UpdateController* self = r->self;
    if (r->ok) {
        self->status_.progress = 100;
        self->restart_at_ms_ = self->clock_.now_ms() + kRestartDelayMs;
        self->set_state(UpdateState::Restarting, "Update installed. Restarting...");
    } else {
        // The running firmware is untouched: the new image was written to
        // the inactive slot and the boot slot was not switched.
        self->set_state(UpdateState::Error, "Update failed: " + r->error);
    }
    delete r;
}

void UpdateController::poll(uint64_t now_ms)
{
    if (!started_) {
        started_ = true;
        refresh_versions();
        next_auto_check_ms_ = now_ms + kFirstAutoCheckDelayMs;
    }

    if (status_.state == UpdateState::Restarting) {
        if (now_ms >= restart_at_ms_) adapter_.restart();
        return;
    }

    if (now_ms >= next_auto_check_ms_) {
        next_auto_check_ms_ = now_ms + kAutoCheckIntervalMs;
        bool busy = status_.state == UpdateState::Checking || status_.state == UpdateState::Installing;
        bool configured = manifest_url_ && !manifest_url_().empty();
        if (!busy && configured && auto_check_ && auto_check_()) check();
    }
}

} // namespace app::application
