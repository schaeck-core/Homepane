#ifndef APP_CPP_UI_ATTRIBUTETEXT_H
#define APP_CPP_UI_ATTRIBUTETEXT_H

#include "../home_assistant/EntityStore.h"

#include <optional>
#include <string>

namespace app::ui {

// Attributes are stored as owned JSON-value text (ADR-0001), e.g. the
// attribute "friendly_name" is literally the text `"Living Room"`
// (quotes included, JSON-escaped) as produced by cJSON_PrintUnformatted.
// Widgets that want to *display* a string-valued attribute need to
// unwrap that JSON text back into plain text — shared here since more
// than one widget needs it (SensorWidget, LightWidget, ...).
std::string json_string_value(const std::string& value_json);

// find_attr(attrs, "friendly_name") == json_string_value(attrs.at("friendly_name")),
// or "" if the attribute is absent.
std::string find_attr(const app::home_assistant::Attributes& attrs, const std::string& name);

// Numeric attributes (e.g. cover's "current_position") arrive as owned
// JSON-value text too (e.g. "70", no quotes since the source value isn't
// a JSON string). Returns `fallback` if the attribute is absent or not
// parseable as an integer.
int find_attr_int(const app::home_assistant::Attributes& attrs, const std::string& name, int fallback);

// Fractional numeric attributes (e.g. climate's "current_temperature",
// "temperature"). std::nullopt if the attribute is absent or not
// parseable as a number — callers decide their own fallback, since
// unlike find_attr_int a missing thermostat setpoint is meaningfully
// different from any real number (there is no natural default).
std::optional<double> find_attr_double(const app::home_assistant::Attributes& attrs, const std::string& name);

} // namespace app::ui

#endif // APP_CPP_UI_ATTRIBUTETEXT_H
