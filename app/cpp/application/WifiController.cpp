#include "WifiController.h"

#include <algorithm>
#include <utility>

namespace app::application {

using app::core::LogLevel;
using app::net::WifiLinkState;
using app::net::WifiNetwork;

WifiController::WifiController(app::net::IWifiAdapter& adapter, app::net::IBackgroundRunner& background,
                               app::core::ITaskRunner& ui, app::core::IMonotonicClock& clock,
                               PersistCredentials persist, app::core::ILogger* logger)
    : adapter_(adapter),
      background_(background),
      ui_(ui),
      clock_(clock),
      persist_(std::move(persist)),
      logger_(logger) {}

std::vector<WifiNetwork> WifiController::normalize(std::vector<WifiNetwork> raw) {
    std::vector<WifiNetwork> out;
    for (auto& n : raw) {
        if (n.ssid.empty()) continue; // hidden network
        auto it = std::find_if(out.begin(), out.end(), [&](const WifiNetwork& o) { return o.ssid == n.ssid; });
        if (it == out.end()) {
            out.push_back(std::move(n));
        } else if (n.rssi > it->rssi) {
            *it = std::move(n);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
    if (out.size() > kMaxScanResults) out.resize(kMaxScanResults);
    return out;
}

bool WifiController::scan(ScanCallback cb) {
    if (scanning_) return false;
    scanning_ = true;
    scan_cb_ = std::move(cb);

    background_.run([this] {
        auto* delivery = new ScanDelivery{this, adapter_.scan(kMaxScanResults * 2)};
        app::core::PostOptions options;
        // Must never be dropped by a full queue: it is what clears scanning_.
        options.critical = true;
        ui_.post(&WifiController::deliver_scan, delivery, options);
    });
    return true;
}

void WifiController::deliver_scan(void* ctx) {
    auto* delivery = static_cast<ScanDelivery*>(ctx);
    WifiController* self = delivery->self;
    std::vector<WifiNetwork> results = normalize(std::move(delivery->results));
    delete delivery;

    self->scanning_ = false;
    ScanCallback cb = std::move(self->scan_cb_);
    self->scan_cb_ = nullptr;
    if (self->logger_) {
        std::string msg = "WifiController: scan found " + std::to_string(results.size()) + " network(s)";
        self->logger_->log(LogLevel::Info, msg.c_str());
    }
    if (cb) cb(results);
}

void WifiController::connect(const std::string& ssid, const std::string& password, ConnectCallback cb) {
    if (connecting_) finish_connect(false, "superseded by a new connection attempt");

    connecting_ = true;
    target_ssid_ = ssid;
    target_password_ = password;
    connect_cb_ = std::move(cb);
    deadline_ms_ = clock_.now_ms() + kConnectTimeoutMs;

    if (logger_) logger_->log(LogLevel::Info, ("WifiController: connecting to " + ssid).c_str());
    if (!adapter_.connect(ssid, password)) finish_connect(false, "WiFi is not available on this device");
}

void WifiController::poll(uint64_t now_ms) {
    if (!connecting_) return;

    app::net::WifiLinkStatus st = adapter_.status();
    if (st.state == WifiLinkState::Connected && st.ssid == target_ssid_) {
        if (persist_) persist_(target_ssid_, target_password_);
        finish_connect(true, "");
    } else if (st.state == WifiLinkState::Failed) {
        finish_connect(false, "Connection failed. Check the password and try again.");
    } else if (now_ms >= deadline_ms_) {
        finish_connect(false, "Connection timed out.");
    }
}

void WifiController::finish_connect(bool ok, const std::string& message) {
    connecting_ = false;
    target_password_.assign(target_password_.size(), '\0');
    target_password_.clear();
    ConnectCallback cb = std::move(connect_cb_);
    connect_cb_ = nullptr;
    if (logger_) {
        logger_->log(ok ? LogLevel::Info : LogLevel::Warn,
                     ok ? ("WifiController: connected to " + target_ssid_).c_str()
                        : ("WifiController: " + message).c_str());
    }
    if (cb) cb(ok, message);
}

} // namespace app::application
