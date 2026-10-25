#ifndef APP_CPP_NET_TIMESYNC_H
#define APP_CPP_NET_TIMESYNC_H

#include <cstddef>
#include <cstdint>
#include <string>

// Where the panel takes its clock from, in this order: the router (the NTP
// server DHCP names, then the gateway itself), Home Assistant (the Date
// header of its web server), the internet (pool.ntp.org) only last and only
// when the setting allows it. The panel should not need the internet.
namespace app::net::timesync {

enum class Source : int { None = 0, Router = 1, HomeAssistant = 2, Internet = 3 };

// The "time_source" setting: "local" never asks the internet; anything
// else (also unset) asks it when nothing local answers.
bool internet_allowed(const char* setting);

constexpr size_t kNtpPacketSize = 48;

// SNTP client request (version 4, mode 3).
void ntp_request(uint8_t out[kNtpPacketSize]);

// A server's reply: its transmit time as Unix seconds. False for anything
// that is not a usable reply (not a server, unsynchronised, kiss-o'-death).
bool ntp_parse_reply(const uint8_t* data, size_t len, double& unix_s);

// An HTTP Date header (IMF-fixdate, "Sun, 06 Nov 1994 08:49:37 GMT") as
// Unix seconds.
bool parse_http_date(const std::string& value, int64_t& unix_s);

} // namespace app::net::timesync

#endif // APP_CPP_NET_TIMESYNC_H
