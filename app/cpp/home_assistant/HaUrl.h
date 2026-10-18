#ifndef APP_CPP_HOME_ASSISTANT_HAURL_H
#define APP_CPP_HOME_ASSISTANT_HAURL_H

#include <string>

namespace app::home_assistant {

// Turns the user-facing Home Assistant base URL stored as `haas_url`
// ("http://192.0.2.10:8123", "https://ha.example.com/") into HA's
// websocket API endpoint ("ws://192.0.2.10:8123/api/websocket",
// "wss://ha.example.com/api/websocket"). Inputs that already are a ws(s)
// URL are only normalized (trailing slash / missing /api/websocket), so
// calling this twice is harmless. A URL without a scheme is treated as
// plain http.
inline std::string to_websocket_url(const std::string& base_url) {
    std::string url = base_url;
    while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r')) url.pop_back();

    std::string rest;
    std::string scheme;
    auto starts_with = [&](const char* prefix) { return url.rfind(prefix, 0) == 0; };
    if (starts_with("https://")) {
        scheme = "wss://";
        rest = url.substr(8);
    } else if (starts_with("http://")) {
        scheme = "ws://";
        rest = url.substr(7);
    } else if (starts_with("wss://")) {
        scheme = "wss://";
        rest = url.substr(6);
    } else if (starts_with("ws://")) {
        scheme = "ws://";
        rest = url.substr(5);
    } else {
        scheme = "ws://";
        rest = url;
    }

    while (!rest.empty() && rest.back() == '/') rest.pop_back();

    const std::string endpoint = "/api/websocket";
    if (rest.size() < endpoint.size() || rest.compare(rest.size() - endpoint.size(), endpoint.size(), endpoint) != 0) {
        rest += endpoint;
    }
    return scheme + rest;
}

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_HAURL_H
