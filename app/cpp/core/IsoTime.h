#ifndef APP_CPP_CORE_ISOTIME_H
#define APP_CPP_CORE_ISOTIME_H

#include <optional>
#include <string>

namespace app::core {

// Home Assistant times -> epoch seconds: "2026-10-08T05:30:00+00:00",
// "...Z", fractions, "2026-10-08 07:30:00" and "2026-10-08" (no offset:
// local time).
std::optional<double> parse_iso_time(const std::string& text);

} // namespace app::core

#endif // APP_CPP_CORE_ISOTIME_H
