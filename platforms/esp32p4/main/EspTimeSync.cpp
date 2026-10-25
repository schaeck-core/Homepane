#include "EspTimeSync.h"

#include "app/config.h"
#include "app/cpp/app_facade.h"
#include "app/cpp/net/TimeSync.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <strings.h>
#include <sys/time.h>
#include <vector>

using app::net::timesync::Source;

namespace {

constexpr const char* TAG = "timesync";
constexpr const char* kInternetServer = "pool.ntp.org";
constexpr uint32_t kResyncMs = 60 * 60 * 1000; // as SNTP did before
constexpr uint32_t kRetryMs = 60 * 1000;       // nothing answered
constexpr int kNtpTimeoutMs = 1500;
constexpr int kNtpTries = 2;
constexpr int kHttpTimeoutMs = 5000;

struct Settings {
    std::string ha_url;
    bool internet = true;
};

std::mutex g_mutex;
Settings g_settings;    // copied on the UI thread, like all config access
Source g_source = Source::None;
std::string g_server;
SemaphoreHandle_t g_settings_read = nullptr;
TaskHandle_t g_task = nullptr;

void read_settings_on_ui(void*)
{
    Settings s;
    const char* url = getConfig("haas_url");
    s.ha_url = url ? url : "";
    while (!s.ha_url.empty() && s.ha_url.back() == '/') s.ha_url.pop_back();
    s.internet = app::net::timesync::internet_allowed(getConfig("time_source"));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_settings = s;
    }
    xSemaphoreGive(g_settings_read);
}

// The UI thread may be busy: then the settings of the last round.
Settings current_settings()
{
    app_cpp_post_to_ui(read_settings_on_ui, nullptr);
    xSemaphoreTake(g_settings_read, pdMS_TO_TICKS(2000));
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_settings;
}

std::string ip_text(const sockaddr_in& a)
{
    char buf[16];
    inet_ntoa_r(a.sin_addr, buf, sizeof(buf));
    return buf;
}

// One SNTP exchange; half the round trip added to the server's time.
bool ntp_query(const sockaddr_in& server, double& unix_s)
{
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return false;
    timeval tv = {kNtpTimeoutMs / 1000, (kNtpTimeoutMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    bool ok = false;
    for (int i = 0; i < kNtpTries && !ok; ++i) {
        uint8_t req[app::net::timesync::kNtpPacketSize];
        app::net::timesync::ntp_request(req);
        const int64_t t0 = esp_timer_get_time();
        if (sendto(s, req, sizeof(req), 0, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) < 0) break;
        uint8_t reply[64];
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(s, reply, sizeof(reply), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        const int64_t t1 = esp_timer_get_time();
        if (n > 0 && from.sin_addr.s_addr == server.sin_addr.s_addr &&
            app::net::timesync::ntp_parse_reply(reply, size_t(n), unix_s)) {
            unix_s += double(t1 - t0) / 2e6;
            ok = true;
        }
    }
    close(s);
    return ok;
}

sockaddr_in ntp_address(uint32_t ip_network_order)
{
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(123);
    a.sin_addr.s_addr = ip_network_order;
    return a;
}

struct RouterServer {
    sockaddr_in address;
    const char* how; // for the log
};

// The NTP server DHCP named, then the gateway itself.
std::vector<RouterServer> router_servers()
{
    std::vector<RouterServer> out;
    const ip_addr_t* dhcp = esp_sntp_getserver(0);
    if (dhcp && IP_IS_V4(dhcp) && !ip4_addr_isany_val(*ip_2_ip4(dhcp)))
        out.push_back({ntp_address(ip_2_ip4(dhcp)->addr), "named by DHCP"});
    esp_netif_ip_info_t info = {};
    esp_netif_t* netif = esp_netif_get_default_netif();
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.gw.addr &&
        (out.empty() || out[0].address.sin_addr.s_addr != info.gw.addr))
        out.push_back({ntp_address(info.gw.addr), "the gateway"});
    return out;
}

esp_err_t on_http_event(esp_http_client_event_t* e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(e->header_key, "Date") == 0)
        *static_cast<std::string*>(e->user_data) = e->header_value;
    return ESP_OK;
}

// Home Assistant's web server dates every answer (also 401/404): a small
// HEAD needs no token and no internet.
bool ha_time(const std::string& base, double& unix_s)
{
    if (base.empty()) return false;
    const std::string url = base + "/manifest.json";
    std::string date;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_HEAD;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = on_http_event;
    cfg.user_data = &date;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    const esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    int64_t seconds = 0;
    if (err != ESP_OK || !app::net::timesync::parse_http_date(date, seconds)) return false;
    unix_s = double(seconds) + 0.5; // the header drops the fraction
    return true;
}

bool internet_time(double& unix_s, std::string& server)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(kInternetServer, "123", &hints, &res) != 0 || !res) return false;
    bool ok = false;
    for (addrinfo* r = res; r && !ok; r = r->ai_next) {
        const sockaddr_in a = *reinterpret_cast<const sockaddr_in*>(r->ai_addr);
        ok = ntp_query(a, unix_s);
        if (ok) server = std::string(kInternetServer) + " (" + ip_text(a) + ")";
    }
    freeaddrinfo(res);
    return ok;
}

const char* source_name(Source s)
{
    switch (s) {
    case Source::Router: return "router";
    case Source::HomeAssistant: return "Home Assistant";
    case Source::Internet: return "internet";
    default: return "nothing";
    }
}

void run(void*)
{
    bool warned = false;
    for (;;) {
        const Settings settings = current_settings();
        Source source = Source::None;
        std::string server;
        const char* how = "";
        double t = 0;
        for (const RouterServer& r : router_servers()) {
            if (ntp_query(r.address, t)) {
                source = Source::Router;
                server = ip_text(r.address);
                how = r.how;
                break;
            }
        }
        if (source == Source::None && ha_time(settings.ha_url, t)) {
            source = Source::HomeAssistant;
            server = settings.ha_url;
        }
        if (source == Source::None && settings.internet && internet_time(t, server)) source = Source::Internet;

        if (source != Source::None) {
            timeval now = {};
            gettimeofday(&now, nullptr);
            const double offset = t - (double(now.tv_sec) + now.tv_usec / 1e6);
            timeval tv = {};
            tv.tv_sec = time_t(t);
            tv.tv_usec = suseconds_t((t - std::floor(t)) * 1e6);
            settimeofday(&tv, nullptr);
            ESP_LOGI(TAG, "time from the %s %s%s%s%s (off by %.2f s)", source_name(source), server.c_str(),
                     *how ? " (" : "", how, *how ? ")" : "", offset);
            warned = false;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_source = source;
            g_server = server;
        } else if (!warned) {
            ESP_LOGW(TAG, "no time: neither router nor Home Assistant answered%s",
                     settings.internet ? ", nor the internet" : " (internet not allowed)");
            warned = true;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(source != Source::None ? kResyncMs : kRetryMs));
    }
}

} // namespace

extern "C" void esp_time_sync_init(void)
{
    esp_sntp_servermode_dhcp(true);
}

extern "C" void esp_time_sync_start(void)
{
    if (g_task) {
        xTaskNotifyGive(g_task);
        return;
    }
    g_settings_read = xSemaphoreCreateBinary();
    // Stack in PSRAM: internal RAM is the scarce one; TLS (https HA) needs
    // the size.
    if (xTaskCreatePinnedToCoreWithCaps(run, "timesync", 8192, nullptr, 2, &g_task, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGW(TAG, "no task: the clock stays unset");
        g_task = nullptr;
    }
}

extern "C" int esp_time_sync_source(char* server, size_t size)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (server && size) std::snprintf(server, size, "%s", g_server.c_str());
    return int(g_source);
}
