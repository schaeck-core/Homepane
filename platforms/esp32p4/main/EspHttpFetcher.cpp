#include "EspHttpFetcher.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

namespace platform_esp32p4 {

namespace {
// One open connection per task that fetches pictures (the image workers
// live as long as the app): covers come from the same server one after the
// other, and a new TCP (or TLS) connection for each cost 300-800 ms.
struct Connection {
    esp_http_client_handle_t client = nullptr;
    std::string origin; // scheme://host:port the connection goes to

    void drop()
    {
        if (client) esp_http_client_cleanup(client);
        client = nullptr;
        origin.clear();
    }
};
thread_local Connection t_connection;

std::string origin_of(const std::string& url)
{
    const size_t scheme = url.find("://");
    const size_t path = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
    return url.substr(0, path);
}

// One GET on `c` (connected again when the server closed it). False: retry
// on a new connection makes sense (nothing usable came back).
bool get_once(Connection& c, const std::string& url, const std::string& bearer, size_t max_bytes, std::string& body,
              std::string& error, bool& retry)
{
    retry = false;
    if (!c.client || c.origin != origin_of(url)) {
        c.drop();
        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.timeout_ms = 15000;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.buffer_size = 4096;
        cfg.keep_alive_enable = true;
        c.client = esp_http_client_init(&cfg);
        if (!c.client) {
            error = "http client init failed";
            return false;
        }
        c.origin = origin_of(url);
    } else {
        esp_http_client_set_url(c.client, url.c_str());
    }
    if (!bearer.empty()) esp_http_client_set_header(c.client, "Authorization", ("Bearer " + bearer).c_str());
    else esp_http_client_delete_header(c.client, "Authorization");
    body.clear();
    if (esp_http_client_open(c.client, 0) != ESP_OK) {
        error = "connect failed";
        retry = true;
        return false;
    }
    const int64_t length = esp_http_client_fetch_headers(c.client);
    if (length < 0) { // the server had closed the kept connection
        error = "no answer";
        retry = true;
        return false;
    }
    const int status = esp_http_client_get_status_code(c.client);
    if (status != 200) {
        error = "HTTP " + std::to_string(status);
        esp_http_client_flush_response(c.client, nullptr);
        return false;
    }
    if (length > static_cast<int64_t>(max_bytes)) {
        error = "too large";
        return false;
    }
    if (length > 0) body.reserve(static_cast<size_t>(length));
    char buf[2048];
    for (;;) {
        const int n = esp_http_client_read(c.client, buf, sizeof(buf));
        if (n < 0) {
            error = "read failed";
            return false;
        }
        if (n == 0) break;
        if (body.size() + static_cast<size_t>(n) > max_bytes) {
            error = "too large";
            return false;
        }
        body.append(buf, static_cast<size_t>(n));
    }
    return esp_http_client_is_complete_data_received(c.client);
}
} // namespace

bool EspHttpFetcher::get(const std::string& url, const std::string& bearer, size_t max_bytes, std::string& body,
                         std::string& error)
{
    Connection& c = t_connection;
    bool retry = false;
    const bool reused = c.client && c.origin == origin_of(url);
    bool ok = get_once(c, url, bearer, max_bytes, body, error, retry);
    if (!ok && retry && reused) { // the kept connection went stale: once more on a new one
        c.drop();
        ok = get_once(c, url, bearer, max_bytes, body, error, retry);
    }
    if (!ok) c.drop(); // a half-read answer would confuse the next request
    return ok;
}

bool EspHttpFetcher::stream(const std::string& url, const std::string& bearer,
                            const std::function<bool(const char* data, size_t size)>& on_data, std::string& error)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 8192;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        error = "http client init failed";
        return false;
    }
    if (!bearer.empty()) esp_http_client_set_header(client, "Authorization", ("Bearer " + bearer).c_str());
    bool ok = false;
    if (esp_http_client_open(client, 0) != ESP_OK) {
        error = "connect failed";
    } else {
        esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            error = "HTTP " + std::to_string(status);
        } else {
            static char buf[8192]; // one stream at a time (the doorbell)
            ok = true;
            for (;;) {
                const int n = esp_http_client_read(client, buf, sizeof(buf));
                if (n < 0) {
                    error = "read failed";
                    ok = false;
                    break;
                }
                if (n == 0 || !on_data(buf, static_cast<size_t>(n))) break;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool EspHttpFetcher::post(const std::string& url, const std::string& bearer, const std::string& content_type,
                          const std::string& body, int& status, std::string& response, std::string& error)
{
    status = 0;
    response.clear();
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.method = HTTP_METHOD_POST;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        error = "http client init failed";
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", content_type.c_str());
    if (!bearer.empty()) esp_http_client_set_header(client, "Authorization", ("Bearer " + bearer).c_str());
    bool ok = false;
    if (esp_http_client_open(client, static_cast<int>(body.size())) != ESP_OK) {
        error = "connect failed";
    } else {
        size_t sent = 0;
        while (sent < body.size()) {
            const int n = esp_http_client_write(client, body.data() + sent, static_cast<int>(body.size() - sent));
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
        if (sent < body.size()) {
            error = "write failed";
        } else {
            esp_http_client_fetch_headers(client);
            status = esp_http_client_get_status_code(client);
            char buf[512];
            for (int n; (n = esp_http_client_read(client, buf, sizeof(buf))) > 0 && response.size() < 4096;) {
                response.append(buf, static_cast<size_t>(n));
            }
            ok = true;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

} // namespace platform_esp32p4
