#ifndef PLATFORMS_SIMULATOR_SIMWIFIADAPTER_H
#define PLATFORMS_SIMULATOR_SIMWIFIADAPTER_H

#include "../../app/cpp/net/IWifiAdapter.h"

#include <mutex>

namespace platform_simulator {

// Desktop stand-in so the WiFi UI (setup screen, settings tab) can be
// developed and tried in the simulator: returns a fixed list of fake
// networks and "connects" after a short delay. Any password works except
// "wrong", which simulates an authentication failure.
class SimWifiAdapter final : public app::net::IWifiAdapter {
public:
    std::vector<app::net::WifiNetwork> scan(size_t max_results) override;
    bool connect(const std::string& ssid, const std::string& password) override;
    app::net::WifiLinkStatus status() const override;

private:
    mutable std::mutex mutex_;
    app::net::WifiLinkStatus status_;
    unsigned attempt_ = 0;
};

class ThreadBackgroundRunner final : public app::net::IBackgroundRunner {
public:
    void run(std::function<void()> job) override;
};

} // namespace platform_simulator

#endif // PLATFORMS_SIMULATOR_SIMWIFIADAPTER_H
