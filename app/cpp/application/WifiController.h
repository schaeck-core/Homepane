#ifndef APP_CPP_APPLICATION_WIFICONTROLLER_H
#define APP_CPP_APPLICATION_WIFICONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../core/ITaskRunner.h"
#include "../net/IWifiAdapter.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app::application {

// WiFi scan/connect flow shared by the setup screen and the settings
// "WiFi" tab (replaces network_setup.c's own scan task / blocking
// network_wifi_init() call). Lives on the UI thread: every callback fires
// on the UI thread (scan results are marshalled back through the
// ITaskRunner), so callers may touch LVGL directly from them.
//
// Credentials are persisted only once the device actually got an IP on
// the requested network — never for a failed attempt, and never because a
// previous connection's state happened to still read "connected".
class WifiController {
public:
    using ScanCallback = std::function<void(const std::vector<app::net::WifiNetwork>&)>;
    using ConnectCallback = std::function<void(bool ok, const std::string& message)>;
    using PersistCredentials = std::function<void(const std::string& ssid, const std::string& password)>;

    static constexpr uint64_t kConnectTimeoutMs = 30000;
    static constexpr size_t kMaxScanResults = 20;

    WifiController(app::net::IWifiAdapter& adapter, app::net::IBackgroundRunner& background,
                    app::core::ITaskRunner& ui, app::core::IMonotonicClock& clock, PersistCredentials persist,
                    app::core::ILogger* logger = nullptr);

    // Starts a scan in the background. Returns false (and never calls cb)
    // if a scan is already running. cb receives the normalized list.
    bool scan(ScanCallback cb);
    bool scanning() const { return scanning_; }

    // Connects to / switches to `ssid`. A previous, still pending connect
    // is completed with ok=false ("superseded") first. cb fires exactly
    // once: on success (IP on the requested network), failure, or timeout.
    void connect(const std::string& ssid, const std::string& password, ConnectCallback cb);
    bool connecting() const { return connecting_; }

    app::net::WifiLinkStatus status() const { return adapter_.status(); }

    void poll(uint64_t now_ms);

    // Drops hidden/empty SSIDs, merges duplicate SSIDs (several access
    // points of one network) keeping the strongest, sorts by signal and
    // caps the list. Public for tests.
    static std::vector<app::net::WifiNetwork> normalize(std::vector<app::net::WifiNetwork> raw);

private:
    struct ScanDelivery {
        WifiController* self;
        std::vector<app::net::WifiNetwork> results;
    };
    static void deliver_scan(void* ctx);
    void finish_connect(bool ok, const std::string& message);

    app::net::IWifiAdapter& adapter_;
    app::net::IBackgroundRunner& background_;
    app::core::ITaskRunner& ui_;
    app::core::IMonotonicClock& clock_;
    PersistCredentials persist_;
    app::core::ILogger* logger_;

    bool scanning_ = false;
    ScanCallback scan_cb_;

    bool connecting_ = false;
    std::string target_ssid_;
    std::string target_password_;
    uint64_t deadline_ms_ = 0;
    ConnectCallback connect_cb_;
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_WIFICONTROLLER_H
