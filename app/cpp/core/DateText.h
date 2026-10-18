#ifndef APP_CPP_CORE_DATETEXT_H
#define APP_CPP_CORE_DATETEXT_H

#include <ctime>
#include <string>

namespace app::core {

// How the user wants dates and times (Settings › Clock & date). The
// language gives the words (weekday, month) and punctuation; the order
// gives where they go:
//   dmy (Europe)  "Mi. 7. Okt."     "Wed 7 Oct"
//   mdy (America) "Mi., Okt. 7"     "Wed, Oct 7"
//   ymd (Asia)    "Okt. 7 (Mi.)"    "Oct 7 (Wed)"   (numeric: 2026-10-07)
enum class DateOrder { dmy, mdy, ymd };

struct DateTimeFormat {
    bool h24 = true;
    DateOrder order = DateOrder::dmy;
};

void set_datetime_format(DateTimeFormat format);
DateTimeFormat datetime_format();
// From the config strings ("24h"/"12h", "eu"/"us"/"iso"); unknown: the defaults.
DateTimeFormat datetime_format_from(const char* time_format, const char* date_format);

struct tm local_tm(double epoch);

// "14:05" / "2:05 PM"
std::string time_text(const struct tm& t);
std::string time_text(double epoch);
// A time on its own in a sentence: "11:00 Uhr" / "11:00 AM"
std::string time_oclock_text(const struct tm& t);
// The clock's digits and its AM/PM apart (the big clock fonts are digits
// only): "14:05" + "" / "2:05" + "PM".
std::string clock_digits(const struct tm& t);
const char* clock_meridiem(const struct tm& t);
// An hour of a forecast: "14" / "2 PM"
std::string hour_text(int hour);
// A time range on one day: "11:00–18:00" / "11:00 AM–6:00 PM" / "2:00–6:00 PM"
std::string time_range_text(const struct tm& start, const struct tm& end);

enum class DateStyle {
    weekday_day_month, // "Mi. 7. Okt."
    long_weekday,      // "Mittwoch, 7. Okt."
    day_month,         // "7. Okt."
    numeric,           // "07.10.2026" / "10/7/2026" / "2026-10-07"
};
std::string date_text(const struct tm& t, DateStyle style);
std::string date_text(double epoch, DateStyle style);

} // namespace app::core

#endif // APP_CPP_CORE_DATETEXT_H
