#include "AttributeText.h"

#include <cjson/cJSON.h>

#include <cstdlib>

namespace app::ui {

std::string json_string_value(const std::string& value_json) {
    if (value_json.empty()) return {};
    cJSON* v = cJSON_Parse(value_json.c_str());
    if (!v) return value_json; // best-effort fallback: show the raw text rather than nothing
    std::string result;
    if (cJSON_IsString(v)) {
        result = v->valuestring;
    } else {
        char* txt = cJSON_PrintUnformatted(v);
        if (txt) {
            result = txt;
            cJSON_free(txt);
        }
    }
    cJSON_Delete(v);
    return result;
}

std::string find_attr(const app::home_assistant::Attributes& attrs, const std::string& name) {
    auto it = attrs.find(name);
    return it == attrs.end() ? std::string{} : json_string_value(it->second);
}

int find_attr_int(const app::home_assistant::Attributes& attrs, const std::string& name, int fallback) {
    std::string text = find_attr(attrs, name);
    if (text.empty()) return fallback;
    // strtol, not std::stoi: ESP-IDF builds with -fno-exceptions, and this
    // helper must work identically on both platforms.
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) return fallback; // no digits consumed
    return static_cast<int>(value);
}

std::optional<double> find_attr_double(const app::home_assistant::Attributes& attrs, const std::string& name) {
    std::string text = find_attr(attrs, name);
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return std::nullopt; // no digits consumed
    return value;
}

} // namespace app::ui
