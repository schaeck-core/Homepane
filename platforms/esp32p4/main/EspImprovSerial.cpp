#include "EspImprovSerial.h"

#include "app/cpp/app_facade.h"
#include "app/cpp/net/ImprovSerial.h"
#include "slave_ota.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include "../../../app/network.h"
}

namespace {

using namespace app::net::improv;

const char* TAG = "improv";
constexpr uart_port_t kPort = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);

// The browser sends its first request once, about a second after the
// reset; this firmware may still be booting then. Until a request arrives,
// the current state is announced every few seconds, which the browser
// accepts as the answer.
constexpr int64_t kAnnounceUntilUs = 30 * 1000 * 1000;
constexpr int64_t kAnnounceEveryUs = 2 * 1000 * 1000;

std::atomic<bool> s_provisioning{false};
// Credentials handed from the UART task to the UI thread; only one
// provisioning runs at a time (s_provisioning).
std::string s_ssid;
std::string s_password;

void send(const std::vector<uint8_t>& packet)
{
    uart_write_bytes(kPort, packet.data(), packet.size());
}

State current_state()
{
    if (s_provisioning.load()) return State::Provisioning;
    return network_is_connected() ? State::Provisioned : State::Ready;
}

// ── UI thread ────────────────────────────────────────────────────────────────

void on_connected(bool ok, const char* message, void*)
{
    s_password.assign(s_password.size(), '\0');
    s_password.clear();
    if (ok) {
        ESP_LOGI(TAG, "connected to %s", s_ssid.c_str());
        send(encode_state(State::Provisioned));
        send(encode_result(Command::WifiSettings, {}));
    } else {
        ESP_LOGW(TAG, "could not connect to %s: %s", s_ssid.c_str(), message ? message : "");
        send(encode_error(Error::UnableToConnect));
        send(encode_state(State::Ready));
    }
    s_provisioning.store(false);
}

void connect_on_ui(void*)
{
    app_cpp_wifi_connect(s_ssid.c_str(), s_password.c_str(), on_connected, nullptr);
}

void on_scanned(const app_cpp_wifi_network_t* networks, int count, void*)
{
    for (int i = 0; i < count; ++i) {
        send(encode_result(Command::ScanNetworks,
                           {networks[i].ssid, std::to_string(networks[i].rssi), networks[i].secured ? "YES" : "NO"}));
    }
    send(encode_result(Command::ScanNetworks, {})); // end of the list
}

void scan_on_ui(void*)
{
    // A scan already running (the panel's own Wi-Fi page): an empty list,
    // the browser then offers to type the network name.
    if (!app_cpp_wifi_scan(on_scanned, nullptr)) send(encode_result(Command::ScanNetworks, {}));
}

// ── UART task ────────────────────────────────────────────────────────────────

void handle(const Rpc& rpc)
{
    switch (static_cast<Command>(rpc.command)) {
        case Command::CurrentState: {
            State state = current_state();
            send(encode_state(state));
            // A provisioned device also answers with its URL (none here).
            if (state == State::Provisioned) send(encode_result(Command::CurrentState, {}));
            break;
        }
        case Command::DeviceInfo:
            send(encode_result(Command::DeviceInfo,
                               {"Homepane", esp_app_get_description()->version, "ESP32-P4", "Homepane"}));
            break;
        case Command::ScanNetworks:
            app_cpp_post_to_ui(scan_on_ui, nullptr);
            break;
        case Command::WifiSettings: {
            std::string ssid, password;
            if (!parse_wifi_settings(rpc.data, ssid, password)) {
                send(encode_error(Error::InvalidRpc));
                break;
            }
            bool idle = false;
            if (!s_provisioning.compare_exchange_strong(idle, true)) break; // the running attempt answers
            s_ssid = ssid;
            s_password = password;
            password.assign(password.size(), '\0');
            ESP_LOGI(TAG, "connecting to %s", s_ssid.c_str());
            send(encode_error(Error::None));
            send(encode_state(State::Provisioning));
            // Not while the C6 receives new firmware (that ends in a restart).
            while (slave_ota_busy()) vTaskDelay(pdMS_TO_TICKS(500));
            app_cpp_post_to_ui(connect_on_ui, nullptr);
            break;
        }
        default:
            send(encode_error(Error::UnknownCommand));
            break;
    }
}

void improv_task(void*)
{
    PacketReader reader;
    bool heard = false;
    int64_t next_announce = 0;
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(kPort, buf, sizeof(buf), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; ++i) {
            switch (reader.feed(buf[i])) {
                case PacketReader::Result::Rpc:
                    heard = true;
                    handle(reader.rpc());
                    break;
                case PacketReader::Result::Invalid:
                    send(encode_error(Error::InvalidRpc));
                    break;
                default:
                    break;
            }
        }
        if (!heard) {
            int64_t now = esp_timer_get_time();
            if (now > kAnnounceUntilUs) {
                heard = true; // nobody asked: stay quiet from now on
            } else if (now >= next_announce) {
                next_announce = now + kAnnounceEveryUs;
                send(encode_state(current_state()));
            }
        }
    }
}

} // namespace

extern "C" void improv_serial_start(void)
{
    // With the driver installed, log output goes through it as well, so log
    // lines and Improv packets are written whole, one after the other.
    esp_err_t err = uart_driver_install(kPort, 512, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "console UART driver: %s (no Improv)", esp_err_to_name(err));
        return;
    }
    uart_vfs_dev_use_driver(kPort);
    // Stack in PSRAM: internal RAM is the scarce one (Wi-Fi's buffers need it),
    // and the task never touches the flash.
    if (xTaskCreatePinnedToCoreWithCaps(improv_task, "improv", 4096, nullptr, 3, nullptr, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGW(TAG, "no task (no Improv)");
    }
}
