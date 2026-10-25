#include "TimeSync.h"

#include <cstdio>
#include <cstring>

namespace app::net::timesync {

namespace {

// 1900-01-01 (NTP era 0) to 1970-01-01
constexpr int64_t kNtpToUnix = 2208988800LL;

uint32_t be32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant).
int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return int64_t(era) * 146097 + doe - 719468;
}

} // namespace

bool internet_allowed(const char* setting)
{
    return !setting || std::strcmp(setting, "local") != 0;
}

void ntp_request(uint8_t out[kNtpPacketSize])
{
    std::memset(out, 0, kNtpPacketSize);
    out[0] = (4 << 3) | 3; // LI 0, version 4, mode 3 (client)
}

bool ntp_parse_reply(const uint8_t* data, size_t len, double& unix_s)
{
    if (!data || len < kNtpPacketSize) return false;
    const int leap = data[0] >> 6;
    const int mode = data[0] & 7;
    const int stratum = data[1];
    if (mode != 4 || leap == 3 || stratum == 0 || stratum > 15) return false;
    const uint32_t seconds = be32(data + 40);
    const uint32_t fraction = be32(data + 44);
    if (seconds == 0) return false;
    // Era 1 starts in 2036: small values belong to it.
    const int64_t ntp = seconds >= 0x80000000u ? int64_t(seconds) : int64_t(seconds) + 0x100000000LL;
    unix_s = double(ntp - kNtpToUnix) + double(fraction) / 4294967296.0;
    return true;
}

bool parse_http_date(const std::string& value, int64_t& unix_s)
{
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char weekday[4] = {}, month[4] = {}, zone[4] = {};
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(value.c_str(), "%3s, %d %3s %d %d:%d:%d %3s", weekday, &day, month, &year, &hour, &minute,
                    &second, zone) != 8)
        return false;
    if (std::strcmp(zone, "GMT") != 0) return false;
    int m = 0;
    while (m < 12 && std::strcmp(month, kMonths[m]) != 0) ++m;
    if (m == 12 || day < 1 || day > 31 || year < 2000 || year > 2200 || hour > 23 || minute > 59 || second > 60 ||
        hour < 0 || minute < 0 || second < 0)
        return false;
    unix_s = days_from_civil(year, m + 1, day) * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

} // namespace app::net::timesync
