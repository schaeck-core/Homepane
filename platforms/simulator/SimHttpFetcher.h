#ifndef PLATFORMS_SIMULATOR_SIMHTTPFETCHER_H
#define PLATFORMS_SIMULATOR_SIMHTTPFETCHER_H

#include "../../app/cpp/net/IHttpFetcher.h"

#include <curl/curl.h>

namespace platform_simulator {

// IHttpFetcher over libcurl (blocking; one easy handle per request).
class SimHttpFetcher final : public app::net::IHttpFetcher {
public:
    bool get(const std::string& url, const std::string& bearer, size_t max_bytes, std::string& body,
             std::string& error) override
    {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "curl init failed";
            return false;
        }
        struct Sink {
            std::string* body;
            size_t max;
            bool too_big = false;
        } sink{&body, max_bytes};
        body.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
            auto* k = static_cast<Sink*>(u);
            if (k->body->size() + s * n > k->max) {
                k->too_big = true;
                return 0;
            }
            k->body->append(p, s * n);
            return s * n;
        });
        curl_slist* headers = nullptr;
        if (!bearer.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        const CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (sink.too_big) {
            error = "too large";
            return false;
        }
        if (rc != CURLE_OK) {
            error = curl_easy_strerror(rc);
            return false;
        }
        if (status != 200 && url.rfind("file://", 0) != 0) { // file: (the demo's pictures) has no status
            error = "HTTP " + std::to_string(status);
            return false;
        }
        return true;
    }

    bool stream(const std::string& url, const std::string& bearer,
                const std::function<bool(const char*, size_t)>& on_data, std::string& error) override
    {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "curl init failed";
            return false;
        }
        struct Sink {
            const std::function<bool(const char*, size_t)>* fn;
            bool stopped = false;
        } sink{&on_data};
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
            auto* k = static_cast<Sink*>(u);
            if (!(*k->fn)(p, s * n)) {
                k->stopped = true;
                return 0; // ends the transfer
            }
            return s * n;
        });
        curl_slist* headers = nullptr;
        if (!bearer.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        const CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (sink.stopped) return true;
        if (rc != CURLE_OK || status != 200) {
            error = rc != CURLE_OK ? curl_easy_strerror(rc) : "HTTP " + std::to_string(status);
            return false;
        }
        return true;
    }

    bool post(const std::string& url, const std::string& bearer, const std::string& content_type,
              const std::string& body, int& status, std::string& response, std::string& error) override
    {
        status = 0;
        response.clear();
        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "curl init failed";
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
            auto* r = static_cast<std::string*>(u);
            if (r->size() < 4096) r->append(p, s * n);
            return s * n;
        });
        curl_slist* headers = curl_slist_append(nullptr, ("Content-Type: " + content_type).c_str());
        if (!bearer.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        const CURLcode rc = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        status = static_cast<int>(code);
        if (rc != CURLE_OK) {
            error = curl_easy_strerror(rc);
            return false;
        }
        return true;
    }
};

} // namespace platform_simulator

#endif // PLATFORMS_SIMULATOR_SIMHTTPFETCHER_H
