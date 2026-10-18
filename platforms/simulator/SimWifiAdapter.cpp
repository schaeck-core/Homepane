#include "SimWifiAdapter.h"

#include <chrono>
#include <thread>

namespace platform_simulator {

std::vector<app::net::WifiNetwork> SimWifiAdapter::scan(size_t max_results) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // feel of a real scan
    std::vector<app::net::WifiNetwork> nets = {
        {"Simulator WiFi", -45, true},
        {"Simulator Guest", -62, false},
        {"Simulator 5G", -71, true},
        {"Simulator Weak", -88, true},
    };
    if (nets.size() > max_results) nets.resize(max_results);
    return nets;
}

bool SimWifiAdapter::connect(const std::string& ssid, const std::string& password) {
    unsigned attempt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attempt = ++attempt_;
        status_ = {app::net::WifiLinkState::Connecting, ssid, ""};
    }
    const bool fail = (password == "wrong");
    std::thread([this, attempt, ssid, fail] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::lock_guard<std::mutex> lock(mutex_);
        if (attempt != attempt_) return; // superseded
        status_ = fail ? app::net::WifiLinkStatus{app::net::WifiLinkState::Failed, ssid, ""}
                       : app::net::WifiLinkStatus{app::net::WifiLinkState::Connected, ssid, "192.0.2.42"};
    }).detach();
    return true;
}

app::net::WifiLinkStatus SimWifiAdapter::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void ThreadBackgroundRunner::run(std::function<void()> job) {
    std::thread(std::move(job)).detach();
}

} // namespace platform_simulator
