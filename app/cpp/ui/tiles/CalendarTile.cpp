#include "CalendarTile.h"

#include "../../core/DateText.h"

#include "ValueTile.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cjson/cJSON.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace app::ui {

namespace {
constexpr size_t kShown = 3;
constexpr uint32_t kRefreshMs = 15 * 60 * 1000;

struct tm local_tm(double epoch)
{
    const time_t t = static_cast<time_t>(epoch);
    struct tm out {};
    localtime_r(&t, &out);
    return out;
}

// whole days between the local dates of a and b (b later: positive)
long local_day_diff(double a, double b)
{
    struct tm ta = local_tm(a), tb = local_tm(b);
    ta.tm_hour = tb.tm_hour = 12; // noon: DST changes stay out of the way
    ta.tm_min = tb.tm_min = ta.tm_sec = tb.tm_sec = 0;
    ta.tm_isdst = tb.tm_isdst = -1;
    return std::lround(std::difftime(mktime(&tb), mktime(&ta)) / 86400.0);
}
} // namespace

CalendarTile::~CalendarTile() { pending_.cancel(); }

std::vector<CalendarTile::Event> CalendarTile::parse_events(const std::string& result_json,
                                                             const std::string& entity_id)
{
    std::vector<Event> out;
    cJSON* root = cJSON_Parse(result_json.c_str());
    if (!root) return out;
    const cJSON* response = cJSON_GetObjectItemCaseSensitive(root, "response");
    const cJSON* entity = cJSON_GetObjectItemCaseSensitive(response, entity_id.c_str());
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(entity, "events"))
    {
        const cJSON* start = cJSON_GetObjectItemCaseSensitive(e, "start");
        const cJSON* summary = cJSON_GetObjectItemCaseSensitive(e, "summary");
        if (!cJSON_IsString(start)) continue;
        const auto when = ValueTile::parse_time(start->valuestring);
        if (!when) continue;
        Event ev;
        ev.start = *when;
        ev.all_day = std::string(start->valuestring).find('T') == std::string::npos &&
                     std::string(start->valuestring).find(' ') == std::string::npos;
        ev.summary = cJSON_IsString(summary) ? summary->valuestring : "";
        out.push_back(std::move(ev));
    }
    cJSON_Delete(root);
    std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
    return out;
}

std::string CalendarTile::when_text(const Event& e, double now)
{
    const long days = local_day_diff(now, e.start);
    std::string day;
    if (days <= 0) {
        day = tr(TR_CALENDAR_TODAY);
    } else if (days == 1) {
        day = tr(TR_CALENDAR_TOMORROW);
    } else if (days < 7) {
        // the tile shows one week: the weekday is enough
        day = tr(static_cast<tr_key_t>(TR_DAY_ABBR_0 + local_tm(e.start).tm_wday % 7));
    } else {
        day = app::core::date_text(e.start, app::core::DateStyle::weekday_day_month);
    }
    if (e.all_day) return day + " · " + tr(TR_CALENDAR_ALL_DAY);
    return day + " · " + app::core::time_text(e.start);
}

void CalendarTile::build(lv_obj_t* tile)
{
    lv_obj_t* top = ui_box(tile);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    name_label_ = ui_label_line(top, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_width(name_label_, LV_PCT(80));
    lv_obj_align(ui_label(top, UI_ICON_BUTTON, UI_TEXT3, LI_CALENDAR), LV_ALIGN_TOP_RIGHT, 0, 0);

    list_ = ui_box(tile);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_, 14, 0);

    refresh_ = lv_timer_create([](lv_timer_t* t) { static_cast<CalendarTile*>(lv_timer_get_user_data(t))->fetch(); },
                               kRefreshMs, this);
}

void CalendarTile::update()
{
    if (!list_) return;
    lv_label_set_text(name_label_, name().c_str());
    lv_obj_clean(list_);
    if (!fetched_) {
        ui_label(list_, UI_FONT_LABEL, UI_TEXT2, "…");
        return;
    }
    if (events_.empty()) {
        ui_label(list_, UI_FONT_LABEL, UI_TEXT2, tr(TR_CALENDAR_NOTHING));
        return;
    }
    const double now = static_cast<double>(std::time(nullptr));
    for (size_t i = 0; i < events_.size() && i < kShown; ++i) {
        lv_obj_t* row = ui_box(list_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(row, 2, 0);
        const bool today = local_day_diff(now, events_[i].start) <= 0;
        ui_label_line(row, UI_FONT_LABEL, today ? UI_ACCENT : UI_TEXT2, when_text(events_[i], now).c_str());
        ui_label_line(row, UI_FONT_BODY, UI_TEXT, events_[i].summary.c_str());
    }
}

void CalendarTile::fetch()
{
    if (!commands()) return;
    pending_.cancel();
    pending_ = commands()->call_service_with_response(
        "calendar", "get_events", entity_id(), R"({"duration":{"days":7}})",
        [this](const app::home_assistant::ResultMessage& r) {
            if (!r.success) return; // keeps what it showed; the next change or refresh tries again
            events_ = parse_events(r.result_json, entity_id());
            fetched_ = true;
            update();
        });
}

void CalendarTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    // the entity changes when an event starts or ends; also the first snapshot
    if (u.entity_state) fetch();
}

void CalendarTile::shutdown_ui()
{
    pending_.cancel();
    if (refresh_) {
        lv_timer_delete(refresh_);
        refresh_ = nullptr;
    }
    list_ = nullptr;
    TileWidget::shutdown_ui();
}

} // namespace app::ui
