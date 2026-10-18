#ifndef PLATFORMS_ESP32P4_MAIN_ESPWIFIADAPTER_H
#define PLATFORMS_ESP32P4_MAIN_ESPWIFIADAPTER_H

#include "../../../app/cpp/net/IWifiAdapter.h"

#include <cstdint>
#include <functional>

namespace platform_esp32p4 {

// IWifiAdapter on top of app/network.c (ESP-Hosted WiFi via the ESP32-C6).
class EspWifiAdapter final : public app::net::IWifiAdapter {
public:
    std::vector<app::net::WifiNetwork> scan(size_t max_results) override;
    bool connect(const std::string& ssid, const std::string& password) override;
    app::net::WifiLinkStatus status() const override;
};

// One short-lived FreeRTOS task per job.
class FreeRtosBackgroundRunner final : public app::net::IBackgroundRunner {
public:
    // 8 KB covers WiFi scans; TLS (HTTPS downloads) needs more.
    // psram_stack: the stack in PSRAM (internal RAM is scarce; only for jobs
    // that never write flash), and a job that cannot get a task is dropped
    // instead of run in the caller — a camera stream would block the UI.
    explicit FreeRtosBackgroundRunner(uint32_t stack_bytes = 8192, bool psram_stack = false)
        : stack_bytes_(stack_bytes), psram_stack_(psram_stack) {}
    void run(std::function<void()> job) override;

private:
    uint32_t stack_bytes_;
    bool psram_stack_;
};

// A fixed set of worker tasks (stacks in PSRAM, core 1, lowest priority)
// taking jobs in order: picture loads, which the image loader already keeps
// to a few at a time. Unlike a task per job, a job is never dropped for
// want of memory for a new task.
class FreeRtosWorkerPool final : public app::net::IBackgroundRunner {
public:
    FreeRtosWorkerPool(int workers, uint32_t stack_bytes) : workers_(workers), stack_bytes_(stack_bytes) {}
    void run(std::function<void()> job) override;
    struct Shared; // the job list the workers share

private:
    void start();
    int workers_;
    uint32_t stack_bytes_;
    Shared* shared_ = nullptr; // made on the first job, never freed (lives as long as the app)
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPWIFIADAPTER_H
