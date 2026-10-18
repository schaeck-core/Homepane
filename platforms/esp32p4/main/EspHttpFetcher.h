#ifndef PLATFORMS_ESP32P4_MAIN_ESPHTTPFETCHER_H
#define PLATFORMS_ESP32P4_MAIN_ESPHTTPFETCHER_H

#include "../../../app/cpp/net/IHttpFetcher.h"

namespace platform_esp32p4 {

// IHttpFetcher over esp_http_client (HTTPS with the certificate bundle).
// Blocking: run it in a background task with a TLS-sized stack.
// get() keeps its connection open per task for the next request: call it
// from long-lived tasks (the image workers), not from short-lived ones.
class EspHttpFetcher final : public app::net::IHttpFetcher {
public:
    bool get(const std::string& url, const std::string& bearer, size_t max_bytes, std::string& body,
             std::string& error) override;
    bool stream(const std::string& url, const std::string& bearer,
                const std::function<bool(const char* data, size_t size)>& on_data, std::string& error) override;
    bool post(const std::string& url, const std::string& bearer, const std::string& content_type,
              const std::string& body, int& status, std::string& response, std::string& error) override;
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPHTTPFETCHER_H
