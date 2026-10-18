#include "ValueTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"
#include "../../core/DateText.h"
#include "../../core/IsoTime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace app::ui {

namespace {
bool parse_number(const std::string& s, double* out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

struct tm local_tm(double epoch)
{
    const time_t t = static_cast<time_t>(epoch);
    struct tm out {};
    localtime_r(&t, &out);
    return out;
}

std::string hm(double epoch) { return app::core::time_text(epoch); }

std::string clock_text(int hour, int minute)
{
    struct tm t {};
    t.tm_hour = hour;
    t.tm_min = minute;
    return app::core::time_text(t);
}

// "7. Okt." / "Oct 7" (settings)
std::string day_month(int day, int month)
{
    struct tm t {};
    t.tm_mday = day;
    t.tm_mon = month - 1;
    return app::core::date_text(t, app::core::DateStyle::day_month);
}

// 252 -> "4:12", 3852 -> "1:04:12"
std::string countdown(double seconds)
{
    long s = seconds > 0 ? static_cast<long>(seconds + 0.5) : 0;
    char buf[16];
    if (s >= 3600) {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", s / 60, s % 60);
    }
    return buf;
}

// HA durations "0:04:12" / "1:04:12" -> seconds
std::optional<double> parse_hms(const std::string& text)
{
    int h = 0, m = 0, sec = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &sec) != 3) return std::nullopt;
    return h * 3600.0 + m * 60.0 + sec;
}

void raw_button_cb(lv_event_t* e)
{
    auto* self = static_cast<ValueTile*>(lv_event_get_user_data(e));
    self->button_pressed(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}
} // namespace

const char* weather_icon(const std::string& c)
{
    if (c == "sunny") return LI_SUN;
    if (c == "clear-night") return LI_MOON;
    if (c == "partlycloudy") return LI_CLOUD_SUN;
    if (c == "cloudy") return LI_CLOUD;
    if (c == "fog") return LI_CLOUD_FOG;
    if (c == "rainy") return LI_CLOUD_RAIN;
    if (c == "pouring") return LI_CLOUD_RAIN;
    if (c == "snowy") return LI_CLOUD_SNOW;
    if (c == "snowy-rainy") return LI_CLOUD_SNOW;
    if (c == "hail") return LI_CLOUD_SNOW;
    if (c == "lightning" || c == "lightning-rainy") return LI_CLOUD_LIGHTNING;
    if (c == "windy" || c == "windy-variant") return LI_WIND;
    if (c == "exceptional") return LI_TRIANGLE_ALERT;
    return LI_CLOUD;
}

const char* weather_text_de(const std::string& c)
{
    if (c == "sunny") return tr(TR_WEATHER_SUNNY);
    if (c == "clear-night") return tr(TR_WEATHER_CLEAR_NIGHT);
    if (c == "partlycloudy") return tr(TR_WEATHER_PARTLYCLOUDY);
    if (c == "cloudy") return tr(TR_WEATHER_CLOUDY);
    if (c == "fog") return tr(TR_WEATHER_FOG);
    if (c == "rainy") return tr(TR_WEATHER_RAINY);
    if (c == "pouring") return tr(TR_WEATHER_POURING);
    if (c == "snowy") return tr(TR_WEATHER_SNOWY);
    if (c == "snowy-rainy") return tr(TR_WEATHER_SNOWY_RAINY);
    if (c == "hail") return tr(TR_WEATHER_HAIL);
    if (c == "lightning") return tr(TR_WEATHER_LIGHTNING);
    if (c == "lightning-rainy") return tr(TR_WEATHER_LIGHTNING_RAINY);
    if (c == "windy" || c == "windy-variant") return tr(TR_WEATHER_WINDY);
    if (c == "exceptional") return tr(TR_WEATHER_EXCEPTIONAL);
    return "";
}

std::optional<double> ValueTile::parse_time(const std::string& text) { return app::core::parse_iso_time(text); }

std::string ValueTile::format_value(const std::string& state)
{
    double v = 0;
    if (!parse_number(state, &v)) return state;
    // keep the given precision, at most one decimal (two below 10)
    const size_t dot = state.find('.');
    int decimals = dot == std::string::npos ? 0 : static_cast<int>(state.size() - dot - 1);
    const int max_decimals = std::fabs(v) < 10 ? 2 : 1;
    if (decimals > max_decimals) decimals = max_decimals;
    std::string out = format_number(v, decimals);
    // "45,0" -> "45", "1,50" -> "1,5"
    const char sep = i18n_decimal();
    if (out.find(sep) != std::string::npos) {
        while (out.back() == '0') out.pop_back();
        if (out.back() == sep) out.pop_back();
    }
    return out;
}

ValueTile::Text ValueTile::text_for(const Facts& f)
{
    Text t;
    if (!f.available) {
        t.value = "–";
        t.unit = "";
        t.status = tr(TR_STATE_UNAVAILABLE);
        return t;
    }
    if (f.domain == "update") {
        const bool pending = f.state == "on";
        t.value = pending ? f.latest_version : f.installed_version;
        t.status = tr(pending ? TR_VALUE_UPDATE_AVAILABLE : TR_VALUE_UP_TO_DATE);
        t.status_color = pending ? UI_ACCENT : 0;
        return t;
    }
    if (f.domain == "timer") {
        const auto finishes = parse_time(f.finishes_at);
        if (f.state == "active" && finishes) {
            t.value = countdown(*finishes - f.now);
            t.status = tr(TR_TIMER_RUNNING);
            t.status_color = UI_ACCENT;
        } else if (f.state == "paused") {
            t.value = countdown(parse_hms(f.remaining).value_or(0));
            t.status = tr(TR_STATE_PAUSED);
        } else {
            t.value = countdown(parse_hms(f.duration).value_or(0));
            t.status = tr(TR_TIMER_IDLE);
        }
        t.icon = LI_TIMER;
        t.icon_color = f.state == "active" ? UI_ACCENT : 0;
        return t;
    }
    if (f.domain == "sun") {
        const bool up = f.state == "above_horizon";
        const auto next = parse_time(up ? f.next_setting : f.next_rising);
        t.value = next ? hm(*next) : "–";
        t.status = tr(up ? TR_SUN_SETS : TR_SUN_RISES);
        t.icon = up ? LI_SUN : LI_MOON;
        t.icon_color = up ? UI_ACCENT : 0;
        return t;
    }
    if (f.domain == "event") {
        t.value = f.event_type.empty() ? "–" : f.event_type;
        const auto when = parse_time(f.state);
        if (when) {
            const double ago = f.now > *when ? f.now - *when : 0;
            t.status = ago < 60 ? std::string(tr(TR_TIME_JUST_NOW)) : trf(TR_TIME_AGO, {format_duration(ago)});
        }
        return t;
    }
    if (f.domain == "date" || f.domain == "time" || f.domain == "datetime" || f.domain == "input_datetime") {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        const bool has_date = f.domain == "date" || f.domain == "datetime" || f.has_date == "true";
        const bool has_time = f.domain == "time" || f.domain == "datetime" || f.has_time == "true";
        t.value = f.state;
        if (f.domain == "datetime") {
            // UTC ISO time: shown in local time
            if (const auto e = parse_time(f.state)) {
                const struct tm l = local_tm(*e);
                t.value = hm(*e);
                t.status = day_month(l.tm_mday, l.tm_mon + 1);
            }
        } else if (has_date && std::sscanf(f.state.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) {
            const std::string date = day_month(d, mo);
            if (has_time && std::sscanf(f.state.c_str() + 10, " %d:%d", &h, &mi) == 2) {
                t.value = clock_text(h, mi);
                t.status = date;
            } else {
                t.value = date;
            }
        } else if (has_time && std::sscanf(f.state.c_str(), "%d:%d", &h, &mi) == 2) {
            t.value = clock_text(h, mi);
        }
        return t;
    }
    if (f.domain == "weather") {
        t.value = f.temperature ? format_number(*f.temperature, 1) : "–";
        t.unit = "°";
        t.status = weather_text_de(f.state);
        t.icon = weather_icon(f.state);
        t.icon_color = UI_ACCENT;
        return t;
    }

    t.value = format_value(f.state);
    t.unit = f.unit;
    double v = 0;
    const bool numeric = parse_number(f.state, &v);
    if (f.domain == "input_number" || f.domain == "number") {
        t.status = tr(TR_VALUE_HELPER);
    } else if (numeric && f.device_class == "carbon_dioxide") {
        t.status = tr(v < 800 ? TR_AIR_GOOD : v < 1200 ? TR_AIR_MODERATE : TR_AIR_VENTILATE);
        t.status_color = v < 800 ? UI_OK : v < 1200 ? 0 : UI_WARN;
    } else if (numeric && f.device_class == "humidity") {
        t.status = tr(v < 35 ? TR_HUMIDITY_DRY : v <= 60 ? TR_HUMIDITY_COMFORTABLE : TR_HUMIDITY_HUMID);
    } else if (f.seconds_in_state) {
        t.status = *f.seconds_in_state < 60 ? std::string(tr(TR_TIME_JUST_UPDATED))
                                             : trf(TR_TIME_SINCE, {format_duration(*f.seconds_in_state)});
    }
    if (!numeric && f.unit.empty()) {
        // text state ("Normal", "idle", ...) fits better as the value itself
        t.unit = "";
    }
    // stale first: the line is cut with "..." when it runs long
    if (f.stale) t.status = tr(TR_STATE_STALE_ONLY) + (t.status.empty() ? std::string() : " · " + t.status);
    return t;
}

ValueTile::Facts ValueTile::facts() const
{
    Facts f;
    f.domain = domain();
    f.state = state();
    f.unit = attr("unit_of_measurement");
    f.device_class = attr("device_class");
    f.latest_version = attr("latest_version");
    f.installed_version = attr("installed_version");
    f.seconds_in_state = seconds_in_state();
    f.temperature = attr_number("temperature");
    f.finishes_at = attr("finishes_at");
    f.remaining = attr("remaining");
    f.duration = attr("duration");
    f.next_rising = attr("next_rising");
    f.next_setting = attr("next_setting");
    f.event_type = attr("event_type");
    f.has_date = attr("has_date");
    f.has_time = attr("has_time");
    f.now = static_cast<double>(std::time(nullptr));
    f.available = available() || !loaded();
    f.stale = entity().availability == app::home_assistant::Availability::stale;
    return f;
}

bool ValueTile::has_buttons() const
{
    const std::string d = domain();
    return d == "input_number" || d == "number" || d == "update" || d == "counter" || d == "timer";
}

void ValueTile::shutdown_ui()
{
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
    TileWidget::shutdown_ui();
}

void ValueTile::build(lv_obj_t* tile)
{
    lv_obj_t* body = ui_box(tile);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* top = ui_box(body);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    name_label_ = ui_label(top, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_width(name_label_, LV_PCT(100)); // narrower only next to an icon (update())
    lv_label_set_long_mode(name_label_, LV_LABEL_LONG_MODE_WRAP);
    icon_label_ = ui_label(top, UI_ICON_VALUE, UI_TEXT3, "");
    lv_obj_align(icon_label_, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t* bottom = ui_box(body);
    lv_obj_set_size(bottom, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bottom, 4, 0);
    lv_obj_t* value_row = ui_box(bottom);
    lv_obj_set_size(value_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(value_row, 6, 0);
    value_label_ = ui_label(value_row, UI_FONT_VALUE, UI_TEXT, "");
    unit_label_ = ui_label(value_row, UI_FONT_BODY, UI_TEXT2, "");
    lv_obj_set_style_pad_bottom(unit_label_, 4, 0); // on the value's baseline
    status_label_ = ui_label_line(bottom, UI_FONT_LABEL, UI_TEXT2, "");

    if (has_buttons()) {
        lv_obj_t* row = ui_button_row(tile);
        const bool update = domain() == "update";
        const bool timer = domain() == "timer";
        for (int i = 0; i < (update ? 1 : 2); ++i) {
            const char* icon = update ? nullptr : timer ? (i == 0 ? LI_PLAY : LI_X) : (i == 0 ? LI_MINUS : LI_PLUS);
            buttons_[i] = ui_tile_button(row, icon, update ? tr(TR_BUTTON_INSTALL) : nullptr, false);
            lv_obj_add_event_cb(buttons_[i], raw_button_cb, LV_EVENT_CLICKED, this);
        }
    }
}

void ValueTile::update()
{
    if (!value_label_) return;
    lv_label_set_text(name_label_, name().c_str());
    if (!loaded()) {
        lv_label_set_text(value_label_, "…");
        lv_label_set_text(unit_label_, "");
        lv_label_set_text(status_label_, "");
        return;
    }
    const Text t = text_for(facts());
    lv_label_set_text(value_label_, t.value.c_str());
    lv_label_set_text(unit_label_, t.unit.c_str());
    lv_label_set_text(status_label_, t.status.c_str());
    lv_obj_set_style_text_color(status_label_, lv_color_hex(t.status_color ? t.status_color : UI_TEXT2), 0);
    lv_label_set_text(icon_label_, t.icon ? t.icon : "");
    lv_obj_set_width(name_label_, t.icon ? LV_PCT(75) : LV_PCT(100));
    lv_obj_set_style_text_color(icon_label_, lv_color_hex(t.icon_color ? t.icon_color : UI_TEXT3), 0);
    // long text states (e.g. "Normal") would not fit the 44 px value font
    lv_obj_set_style_text_font(value_label_, t.value.size() > 7 ? UI_FONT_TITLE : UI_FONT_VALUE, 0);
    if (domain() == "timer") {
        const bool active = state() == "active";
        if (buttons_[0]) ui_tile_button_set(buttons_[0], active ? LI_PAUSE : LI_PLAY, nullptr, active);
        // count down while running
        if (active && !tick_) {
            tick_ = lv_timer_create([](lv_timer_t* t) { static_cast<ValueTile*>(lv_timer_get_user_data(t))->update(); },
                                    1000, this);
        } else if (!active && tick_) {
            lv_timer_delete(tick_);
            tick_ = nullptr;
        }
    }
    if (domain() == "update" && buttons_[0]) {
        if (state() == "on") lv_obj_remove_flag(lv_obj_get_parent(buttons_[0]), LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lv_obj_get_parent(buttons_[0]), LV_OBJ_FLAG_HIDDEN);
    }
}

void ValueTile::on_tap()
{
    // History / details come with the detail views; a tap shows the exact value.
    if (!loaded() || !available()) return;
    const std::string unit = attr("unit_of_measurement");
    ui_toast((name() + ": " + state() + (unit.empty() ? "" : " " + unit)).c_str());
}

void ValueTile::button_pressed(int index)
{
    const std::string d = domain();
    if (d == "update") {
        call("install");
        ui_toast(tr(TR_VALUE_INSTALLING));
        return;
    }
    if (d == "input_number" || d == "counter") {
        call(index == 0 ? "decrement" : "increment");
        return;
    }
    if (d == "timer") {
        if (index == 1) {
            call("cancel");
        } else {
            call(state() == "active" ? "pause" : "start");
        }
        return;
    }
    // number: no increment service; set_value with the step
    double v = 0;
    if (!parse_number(state(), &v)) return;
    const double step = attr_number("step").value_or(1.0);
    double next = v + (index == 0 ? -step : step);
    if (auto mn = attr_number("min")) next = std::fmax(next, *mn);
    if (auto mx = attr_number("max")) next = std::fmin(next, *mx);
    char json[48];
    std::snprintf(json, sizeof(json), "{\"value\":%g}", next); // JSON: decimal point, not comma
    call("set_value", json);
}

} // namespace app::ui
