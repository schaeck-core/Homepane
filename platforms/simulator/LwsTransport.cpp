#include "LwsTransport.h"

#include <libwebsockets.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace platform_simulator {

namespace {

// Same minimal "ws://host:port/path" parsing as the legacy
// app/websocket_client.c::parse_ws_url(), reimplemented here since the
// original is file-local (static) and this adapter must not reach into
// the legacy file (C++ migration phase 3: new transport is
// self-contained, not built on top of the old one).
void parse_ws_url(const std::string& url, std::string& host, int& port, std::string& path) {
    host.clear();
    port = 80;
    path = "/ws";
    if (url.empty()) return;

    size_t scheme_pos = url.find("://");
    size_t h_start = (scheme_pos == std::string::npos) ? 0 : scheme_pos + 3;
    size_t slash = url.find('/', h_start);
    size_t colon = url.find(':', h_start);
    bool has_port = (colon != std::string::npos) && (slash == std::string::npos || colon < slash);

    size_t host_end = has_port ? colon : (slash == std::string::npos ? url.size() : slash);
    host = url.substr(h_start, host_end - h_start);

    if (has_port) {
        size_t port_end = (slash == std::string::npos) ? url.size() : slash;
        port = std::atoi(url.substr(colon + 1, port_end - colon - 1).c_str());
    }
    if (slash != std::string::npos) path = url.substr(slash);
}

// Free function matching lws' exact expected callback signature; forwards
// to the header-friendly (libwebsockets-agnostic) trampoline below so
// LwsTransport.h never needs to include libwebsockets.h.
int raw_lws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    return LwsTransport::lws_callback_trampoline(wsi, static_cast<int>(reason), user, in, len);
}

struct lws_protocols g_protocols[] = {
    {"http", raw_lws_callback, 0, 524288, 0, nullptr, 0},
    {nullptr, nullptr, 0, 0, 0, nullptr, 0}
};

} // namespace

LwsTransport::LwsTransport() = default;

LwsTransport::~LwsTransport() { close(); }

void LwsTransport::set_sink(app::net::IWebSocketTransportSink* sink) { sink_ = sink; }

void LwsTransport::connect(const std::string& url) {
    closing_ = false;

    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = g_protocols;
    info.gid = -1;
    info.uid = -1;

    context_ = lws_create_context(&info);
    if (!context_) {
        if (sink_) sink_->on_transport_error("lws_create_context failed");
        return;
    }

    std::string host, path;
    int port;
    parse_ws_url(url, host, port, path);

    struct lws_client_connect_info i;
    std::memset(&i, 0, sizeof(i));
    i.context = context_;
    i.address = host.c_str();
    i.port = port;
    i.path = path.c_str();
    i.host = i.address;
    i.origin = i.address;
    i.protocol = g_protocols[0].name;
    i.ssl_connection = 0;
    i.userdata = this;

    wsi_ = lws_client_connect_via_info(&i);
    if (!wsi_) {
        lws_context_destroy(context_);
        context_ = nullptr;
        if (sink_) sink_->on_transport_error("lws_client_connect_via_info failed");
    }
}

void LwsTransport::close() {
    if (closing_) return;
    closing_ = true;
    // Stop delivering events before tearing down: lws_context_destroy()
    // may synchronously fire callbacks for the still-open wsi, and a
    // close initiated by our own destructor must not call back into a
    // WebSocketConnection that may itself be mid-teardown.
    sink_ = nullptr;
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
    wsi_ = nullptr;
}

bool LwsTransport::send_text(const uint8_t* data, size_t len) {
    if (!wsi_) return false;
    std::vector<unsigned char> buf(LWS_PRE + len);
    std::memcpy(buf.data() + LWS_PRE, data, len);
    int n = lws_write(wsi_, buf.data() + LWS_PRE, len, LWS_WRITE_TEXT);
    return n == static_cast<int>(len);
}

bool LwsTransport::send_binary(const uint8_t* data, size_t len) {
    if (!wsi_) return false;
    std::vector<unsigned char> buf(LWS_PRE + len);
    std::memcpy(buf.data() + LWS_PRE, data, len);
    int n = lws_write(wsi_, buf.data() + LWS_PRE, len, LWS_WRITE_BINARY);
    return n == static_cast<int>(len);
}

void LwsTransport::request_writable() {
    if (wsi_) lws_callback_on_writable(wsi_);
}

void LwsTransport::service() {
    if (context_) lws_service(context_, -1);
}

int LwsTransport::lws_callback_trampoline(struct lws* wsi, int reason, void* user, void* in, size_t len) {
    (void)user;
    auto* self = wsi ? reinterpret_cast<LwsTransport*>(lws_wsi_user(wsi)) : nullptr;
    if (self) self->handle_callback(wsi, reason, in, len);
    return 0;
}

void LwsTransport::handle_callback(struct lws* wsi, int reason_int, void* in, size_t len) {
    auto reason = static_cast<enum lws_callback_reasons>(reason_int);
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (sink_) sink_->on_transport_open();
            break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            if (sink_) sink_->on_transport_error(in ? static_cast<const char*>(in) : "connection error");
            break;
        case LWS_CALLBACK_CLOSED:
            if (sink_) sink_->on_transport_closed("closed");
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            bool is_final = lws_is_final_fragment(wsi);
            if (sink_) sink_->on_transport_fragment(reinterpret_cast<const uint8_t*>(in), len, is_final);
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (sink_) sink_->on_transport_writable();
            break;
        default:
            break;
    }
}

} // namespace platform_simulator
