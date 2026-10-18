#include "EspWifiAdapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <deque>
#include <memory>
#include <mutex>

extern "C" {
#include "../../../app/network.h"
}

namespace platform_esp32p4 {

std::vector<app::net::WifiNetwork> EspWifiAdapter::scan(size_t max_results) {
    std::vector<app::net::WifiNetwork> out;
    if (max_results == 0) return out;
    std::unique_ptr<network_ap_record_t[]> recs(new network_ap_record_t[max_results]());
    int n = network_wifi_scan(recs.get(), static_cast<uint16_t>(max_results));
    for (int i = 0; i < n; ++i) {
        out.push_back({recs[i].ssid, recs[i].rssi, recs[i].auth_mode != 0});
    }
    return out;
}

bool EspWifiAdapter::connect(const std::string& ssid, const std::string& password) {
    return network_wifi_connect_async(ssid.c_str(), password.c_str());
}

app::net::WifiLinkStatus EspWifiAdapter::status() const {
    network_info_t info = network_get_info();
    app::net::WifiLinkStatus st;
    if (info.type != NET_TYPE_WIFI) return st; // Ethernet or nothing: WiFi itself is down
    st.ssid = info.ssid;
    switch (info.status) {
        case NET_STATUS_CONNECTED:
            st.state = app::net::WifiLinkState::Connected;
            st.ip = info.ip;
            break;
        case NET_STATUS_CONNECTING:
            st.state = app::net::WifiLinkState::Connecting;
            break;
        case NET_STATUS_ERROR:
            st.state = app::net::WifiLinkState::Failed;
            break;
        default:
            st.state = app::net::WifiLinkState::Disconnected;
            break;
    }
    return st;
}

namespace {
void job_trampoline(void* arg) {
    auto* job = static_cast<std::function<void()>*>(arg);
    (*job)();
    delete job;
    vTaskDelete(nullptr);
}

void psram_job_trampoline(void* arg) {
    auto* job = static_cast<std::function<void()>*>(arg);
    (*job)();
    delete job;
    vTaskDeleteWithCaps(nullptr);
}
} // namespace

void FreeRtosBackgroundRunner::run(std::function<void()> job) {
    if (psram_stack_) {
        auto* heap_job = new std::function<void()>(std::move(job));
        // core 1 at the lowest task priority: a camera stream decodes without a
        // pause, and must never take core 0 from the app's main loop (prio 1,
        // pinned there) or delay LVGL's rendering
        if (xTaskCreatePinnedToCoreWithCaps(psram_job_trampoline, "bg_media", stack_bytes_, heap_job, 1, nullptr, 1,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            ESP_LOGE("bg_job", "no task for a background job (%u bytes stack): dropped", (unsigned)stack_bytes_);
            delete heap_job;
        }
        return;
    }
    auto* heap_job = new std::function<void()>(std::move(job));
    if (xTaskCreate(job_trampoline, "bg_job", stack_bytes_, heap_job, 5, nullptr) != pdPASS) {
        // Better late than never: run inline rather than dropping the job
        // (whose completion callback the caller is waiting for).
        (*heap_job)();
        delete heap_job;
    }
}

struct FreeRtosWorkerPool::Shared {
    std::mutex mutex;
    std::deque<std::function<void()>> jobs;
    SemaphoreHandle_t ready = nullptr; // counts the jobs
};

namespace {
void pool_worker(void* arg) {
    auto* shared = static_cast<FreeRtosWorkerPool::Shared*>(arg);
    for (;;) {
        xSemaphoreTake(shared->ready, portMAX_DELAY);
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->jobs.empty()) continue;
            job = std::move(shared->jobs.front());
            shared->jobs.pop_front();
        }
        job();
    }
}
} // namespace

void FreeRtosWorkerPool::start() {
    shared_ = new Shared();
    shared_->ready = xSemaphoreCreateCounting(1024, 0);
    for (int i = 0; i < workers_; ++i) {
        if (xTaskCreatePinnedToCoreWithCaps(pool_worker, "img_worker", stack_bytes_, shared_, 1, nullptr, 1,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            ESP_LOGE("img_pool", "worker %d not started", i);
        }
    }
}

void FreeRtosWorkerPool::run(std::function<void()> job) {
    if (!shared_) start();
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->jobs.push_back(std::move(job));
    }
    xSemaphoreGive(shared_->ready);
}

} // namespace platform_esp32p4
