#ifndef APP_CPP_NET_IHTTPFETCHER_H
#define APP_CPP_NET_IHTTPFETCHER_H

#include <cstddef>
#include <functional>
#include <string>

namespace app::net {

// Per-platform HTTP(S) GET (simulator: curl; ESP32-P4: esp_http_client with
// the certificate bundle). Blocking — called only through an
// IBackgroundRunner, never on the UI thread.
class IHttpFetcher {
public:
    virtual ~IHttpFetcher() = default;

    // bearer: sent as "Authorization: Bearer ..." when not empty. Fails when
    // the status is not 200 or the body exceeds max_bytes.
    virtual bool get(const std::string& url, const std::string& bearer, size_t max_bytes, std::string& body,
                     std::string& error) = 0;

    // A long response read as it arrives (e.g. an MJPEG camera stream):
    // on_data gets each chunk; returning false ends the request. Returns
    // when it ended (false with error when it failed to start or broke off).
    virtual bool stream(const std::string& url, const std::string& bearer,
                        const std::function<bool(const char* data, size_t size)>& on_data, std::string& error) = 0;

    // POST body (e.g. a multipart upload). status: the HTTP status (0 when
    // there was no answer); response: the answer's body (small). True when
    // the request went through (any status).
    virtual bool post(const std::string& url, const std::string& bearer, const std::string& content_type,
                      const std::string& body, int& status, std::string& response, std::string& error) = 0;
};

} // namespace app::net

#endif // APP_CPP_NET_IHTTPFETCHER_H
