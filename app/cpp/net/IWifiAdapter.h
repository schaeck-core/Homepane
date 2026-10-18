#ifndef APP_CPP_NET_IWIFIADAPTER_H
#define APP_CPP_NET_IWIFIADAPTER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app::net {

struct WifiNetwork {
    std::string ssid;
    int rssi = 0;           // dBm
    bool secured = false;   // false = open network, no password
};

enum class WifiLinkState {
    Disconnected,
    Connecting,
    Connected,  // associated and got an IP
    Failed,     // last attempt gave up (e.g. wrong password)
};

struct WifiLinkStatus {
    WifiLinkState state = WifiLinkState::Disconnected;
    std::string ssid;  // network connected/connecting to, "" if none
    std::string ip;    // only while Connected
};

// Thin per-platform WiFi shim (ESP32-P4: ESP-Hosted via app/network.c;
// simulator: fake networks). Knows nothing about UI or persistence.
class IWifiAdapter {
public:
    virtual ~IWifiAdapter() = default;

    // May block for several seconds — WifiController only ever calls it
    // through IBackgroundRunner, never on the UI thread.
    virtual std::vector<WifiNetwork> scan(size_t max_results) = 0;

    // Must NOT block: starts connecting/switching and returns. From the
    // moment it returns, status() must report Connecting for `ssid` (never
    // a stale Connected/Failed of a previous attempt). false = could not
    // start at all (e.g. no WiFi hardware).
    virtual bool connect(const std::string& ssid, const std::string& password) = 0;

    // Cheap, callable from the UI thread.
    virtual WifiLinkStatus status() const = 0;
};

// Runs a job off the UI thread (FreeRTOS task / std::thread).
class IBackgroundRunner {
public:
    virtual ~IBackgroundRunner() = default;
    virtual void run(std::function<void()> job) = 0;
};

} // namespace app::net

#endif // APP_CPP_NET_IWIFIADAPTER_H
