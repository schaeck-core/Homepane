#include "IsoTime.h"

#include <cstdio>
#include <ctime>

namespace app::core {

namespace {
// Days since 1970-01-01 of a civil date (no timegm() in newlib).
long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}
} // namespace

std::optional<double> parse_iso_time(const std::string& text)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, n = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d%n", &y, &mo, &d, &n) != 3) return std::nullopt;
    const char* rest = text.c_str() + n;
    if (*rest == 'T' || *rest == ' ') {
        int used = 0;
        if (std::sscanf(rest + 1, "%d:%d:%d%n", &h, &mi, &sec, &used) < 2) return std::nullopt;
        rest += 1 + used;
        while (*rest == '.' || (*rest >= '0' && *rest <= '9')) ++rest; // fractions
    } else {
        rest = "";
    }
    const double civil = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400.0 +
                         h * 3600.0 + mi * 60.0 + sec;
    if (*rest == 'Z') return civil;
    if (*rest == '+' || *rest == '-') {
        int oh = 0, om = 0;
        std::sscanf(rest + 1, "%d:%d", &oh, &om);
        const double offset = oh * 3600.0 + om * 60.0;
        return *rest == '+' ? civil - offset : civil + offset;
    }
    // no offset: local time
    struct tm l {};
    l.tm_year = y - 1900;
    l.tm_mon = mo - 1;
    l.tm_mday = d;
    l.tm_hour = h;
    l.tm_min = mi;
    l.tm_sec = sec;
    l.tm_isdst = -1;
    return static_cast<double>(mktime(&l));
}

} // namespace app::core
