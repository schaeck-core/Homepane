#include "DateText.h"

#include "../../i18n.h"

#include <cstdio>
#include <cstring>

namespace app::core {

namespace {
DateTimeFormat g_format;

int order_index(DateOrder o) { return o == DateOrder::mdy ? 1 : o == DateOrder::ymd ? 2 : 0; }

int hour12(int h) { return h % 12 == 0 ? 12 : h % 12; }
const char* meridiem(int h) { return h < 12 ? "AM" : "PM"; }

std::string hm(int h, int m)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}
} // namespace

void set_datetime_format(DateTimeFormat format) { g_format = format; }
DateTimeFormat datetime_format() { return g_format; }

DateTimeFormat datetime_format_from(const char* time_format, const char* date_format)
{
    DateTimeFormat f;
    f.h24 = !(time_format && std::strcmp(time_format, "12h") == 0);
    if (date_format && std::strcmp(date_format, "us") == 0) f.order = DateOrder::mdy;
    else if (date_format && std::strcmp(date_format, "iso") == 0) f.order = DateOrder::ymd;
    return f;
}

struct tm local_tm(double epoch)
{
    const time_t t = static_cast<time_t>(epoch);
    struct tm out {};
    localtime_r(&t, &out);
    return out;
}

std::string clock_digits(const struct tm& t)
{
    if (g_format.h24) return hm(t.tm_hour, t.tm_min);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d:%02d", hour12(t.tm_hour), t.tm_min);
    return buf;
}

const char* clock_meridiem(const struct tm& t) { return g_format.h24 ? "" : meridiem(t.tm_hour); }

std::string time_text(const struct tm& t)
{
    if (g_format.h24) return hm(t.tm_hour, t.tm_min);
    return clock_digits(t) + " " + meridiem(t.tm_hour);
}

std::string time_text(double epoch) { return time_text(local_tm(epoch)); }

std::string time_oclock_text(const struct tm& t)
{
    return g_format.h24 ? trf(TR_TIME_OCLOCK, {hm(t.tm_hour, t.tm_min)}) : time_text(t);
}

std::string hour_text(int hour)
{
    if (g_format.h24) return std::to_string(hour);
    return std::to_string(hour12(hour)) + " " + meridiem(hour);
}

std::string time_range_text(const struct tm& start, const struct tm& end)
{
    if (g_format.h24) return trf(TR_TIME_OCLOCK, {hm(start.tm_hour, start.tm_min) + "–" + hm(end.tm_hour, end.tm_min)});
    // "2:00–6:00 PM": one AM/PM when both share it
    if ((start.tm_hour < 12) == (end.tm_hour < 12)) return clock_digits(start) + "–" + time_text(end);
    return time_text(start) + "–" + time_text(end);
}

std::string date_text(const struct tm& t, DateStyle style)
{
    const int o = order_index(g_format.order);
    if (style == DateStyle::numeric) {
        char buf[16];
        const int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
        if (o == 1) std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d", m, d, y);
        else if (o == 2) std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
        else std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d", d, m, y);
        return buf;
    }
    tr_key_t key = TR_DATE_CLOCK_DMY;
    if (style == DateStyle::long_weekday) key = TR_DATE_LONG_DMY;
    else if (style == DateStyle::day_month) key = TR_DATE_DAY_MONTH_DMY;
    // the three orders follow each other in the key table (as in the .lang files)
    key = static_cast<tr_key_t>(key + o);
    const tr_key_t day_names = style == DateStyle::long_weekday ? TR_DAY_LONG_0 : TR_DAY_ABBR_0;
    return trf(key, {tr(static_cast<tr_key_t>(day_names + t.tm_wday % 7)),
                     tr(static_cast<tr_key_t>(TR_MONTH_ABBR_0 + t.tm_mon % 12)), std::to_string(t.tm_mday)});
}

std::string date_text(double epoch, DateStyle style) { return date_text(local_tm(epoch), style); }

} // namespace app::core
