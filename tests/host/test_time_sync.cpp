// Unit tests for the time sync building blocks: the "time_source" setting,
// SNTP packets and the HTTP Date header Home Assistant's web server sends.

#include "app/cpp/net/TimeSync.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace app::net::timesync;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

// A server reply: mode 4, stratum 2, the given transmit time.
void server_reply(uint8_t p[kNtpPacketSize], uint32_t seconds, uint32_t fraction)
{
    for (size_t i = 0; i < kNtpPacketSize; ++i) p[i] = 0;
    p[0] = (4 << 3) | 4;
    p[1] = 2;
    for (int i = 0; i < 4; ++i) {
        p[40 + i] = uint8_t(seconds >> (24 - 8 * i));
        p[44 + i] = uint8_t(fraction >> (24 - 8 * i));
    }
}

} // namespace

int main()
{
    expect(internet_allowed(nullptr), "unset setting: the internet as the last resort");
    expect(internet_allowed("auto"), "auto: the internet as the last resort");
    expect(!internet_allowed("local"), "local: never the internet");

    uint8_t p[kNtpPacketSize];
    ntp_request(p);
    expect(p[0] == 0x23, "request: version 4, client mode");
    bool rest_zero = true;
    for (size_t i = 1; i < kNtpPacketSize; ++i) rest_zero &= p[i] == 0;
    expect(rest_zero, "request: everything else zero");

    // 2026-10-24 12:00:00 UTC
    const int64_t unix_noon = 1792843200LL;
    double t = 0;
    server_reply(p, uint32_t(unix_noon + 2208988800LL), 0x80000000u);
    expect(ntp_parse_reply(p, sizeof(p), t) && std::fabs(t - (double(unix_noon) + 0.5)) < 1e-6,
           "reply: transmit time and fraction");
    expect(!ntp_parse_reply(p, sizeof(p) - 1, t), "reply: too short");
    p[0] = (3 << 6) | (4 << 3) | 4;
    expect(!ntp_parse_reply(p, sizeof(p), t), "reply: unsynchronised server (LI 3)");
    server_reply(p, uint32_t(unix_noon + 2208988800LL), 0);
    p[1] = 0;
    expect(!ntp_parse_reply(p, sizeof(p), t), "reply: kiss-o'-death (stratum 0)");
    server_reply(p, uint32_t(unix_noon + 2208988800LL), 0);
    p[0] = (4 << 3) | 3;
    expect(!ntp_parse_reply(p, sizeof(p), t), "reply: not from a server");
    server_reply(p, 0, 0);
    expect(!ntp_parse_reply(p, sizeof(p), t), "reply: no transmit time");
    server_reply(p, 1000, 0); // NTP era 1 (2036-02-07 06:28:16 UTC + 1000 s)
    expect(ntp_parse_reply(p, sizeof(p), t) && t == 2085978496.0 + 1000.0, "reply: after the 2036 rollover");

    int64_t s = 0;
    expect(parse_http_date("Sat, 24 Oct 2026 12:00:00 GMT", s) && s == unix_noon, "Date: IMF-fixdate");
    expect(parse_http_date("Thu, 29 Feb 2024 23:59:59 GMT", s) && s == 1709251199LL, "Date: leap day");
    expect(!parse_http_date("Sat, 24 Oct 2026 12:00:00 CET", s), "Date: only GMT");
    expect(!parse_http_date("Sat, 24 Okt 2026 12:00:00 GMT", s), "Date: English month names only");
    expect(!parse_http_date("Saturday, 24-Oct-26 12:00:00 GMT", s), "Date: obsolete RFC 850 form refused");
    expect(!parse_http_date("", s), "Date: empty");

    if (g_failures) std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
