#include "ChartTile.h"

#include "../AttributeText.h"
#include "TileWidget.h"
#include "ValueTile.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace app::ui {

ChartTile::ChartTile(app::home_assistant::HaInstanceId instance, app::home_assistant::StatisticsQuery query,
                     std::string title)
    : instance_(std::move(instance)), query_(std::move(query)), title_(std::move(title))
{
}

std::vector<int> ChartTile::bar_heights(const std::vector<double>& means)
{
    if (means.empty()) return {};
    const size_t n = std::min(means.size(), kBars);
    std::vector<double> last(means.end() - static_cast<long>(n), means.end());
    double max = 0;
    for (double v : last) max = std::max(max, v);
    // hours without data yet are on the left, as if they were 0: the bars keep their width
    std::vector<int> out(kBars - n, 4);
    for (double v : last) out.push_back(max > 0 ? std::max(4, static_cast<int>(std::lround(v / max * 100))) : 4);
    return out;
}

std::pair<std::string, std::string> ChartTile::value_text(const std::string& state, const std::string& unit)
{
    char* end = nullptr;
    const double v = std::strtod(state.c_str(), &end);
    if (state.empty() || !end || *end != '\0') return {state.empty() ? "–" : state, unit};
    if (unit == "W" && std::fabs(v) >= 1000) return {format_number(v / 1000.0, 2), "kW"};
    if (unit == "Wh" && std::fabs(v) >= 1000) return {format_number(v / 1000.0, 2), "kWh"};
    return {ValueTile::format_value(state), unit};
}

std::vector<DataRequirement> ChartTile::requirements() const
{
    std::vector<DataRequirement> out;
    out.push_back(StatisticsRequirement{instance_, query_, 300000}); // hourly buckets: every 5 min is plenty
    if (!query_.entity_ids.empty()) out.push_back(EntityRequirement{instance_, query_.entity_ids.front()});
    return out;
}

void ChartTile::create(lv_obj_t* parent)
{
    lv_obj_t* tile = ui_tile(parent);
    lv_obj_set_style_pad_bottom(tile, 20, 0);
    lv_obj_set_style_pad_row(tile, 10, 0);
    title_label_ = ui_label_line(tile, UI_FONT_NAME, UI_TEXT, title_.c_str());

    lv_obj_t* value_row = ui_box(tile);
    lv_obj_set_size(value_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(value_row, 6, 0);
    value_label_ = ui_label(value_row, UI_FONT_VALUE, UI_TEXT, "…");
    unit_label_ = ui_label(value_row, UI_FONT_BODY, UI_TEXT2, "");
    lv_obj_set_style_pad_bottom(unit_label_, 4, 0);

    bars_ = ui_box(tile);
    lv_obj_set_width(bars_, LV_PCT(100));
    lv_obj_set_flex_grow(bars_, 1);
    lv_obj_set_flex_flow(bars_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(bars_, 3, 0);

    // empty state: a word and a dashed line instead of a blank chart
    empty_ = ui_box(tile);
    lv_obj_add_flag(empty_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(empty_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(empty_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(empty_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(empty_, 14, 0);
    ui_label(empty_, UI_FONT_LABEL, UI_TEXT3, tr(TR_DASHBOARD_NO_DATA));
    lv_obj_t* dashes = ui_box(empty_);
    lv_obj_set_size(dashes, LV_PCT(100), 2);
    lv_obj_set_flex_flow(dashes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dashes, 6, 0);
    for (int i = 0; i < 16; ++i) {
        lv_obj_t* d = ui_box(dashes);
        lv_obj_set_size(d, 8, 2);
        lv_obj_set_style_bg_color(d, lv_color_hex(UI_BAR), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);

    render_bars();
    track_root(tile);
}

void ChartTile::on_data(const DataUpdate& u)
{
    if (u.statistics_result) {
        if (!u.statistics_result->has_data) return; // the cached "nothing yet" before the first response
        has_statistics_ = true;
        means_.clear();
        for (const auto& series : u.statistics_result->series) {
            if (series.stat_type != "mean" || (!query_.entity_ids.empty() && series.entity_id != query_.entity_ids[0]))
                continue;
            for (const auto& p : series.points) means_.push_back(p.value);
        }
        render_bars();
    } else if (u.entity_state && value_label_) {
        const auto& s = *u.entity_state;
        if (title_.empty()) {
            title_ = find_attr(s.attributes, "friendly_name");
            lv_label_set_text(title_label_, title_.c_str());
        }
        const auto [value, unit] = value_text(s.state == "unavailable" ? "" : s.state,
                                              find_attr(s.attributes, "unit_of_measurement"));
        lv_label_set_text(value_label_, value.c_str());
        lv_label_set_text(unit_label_, unit.c_str());
    }
}

void ChartTile::render_bars()
{
    if (!bars_) return;
    lv_obj_clean(bars_);
    const std::vector<int> heights = bar_heights(means_);
    if (heights.empty()) {
        if (has_statistics_) lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    for (size_t i = 0; i < heights.size(); ++i) {
        lv_obj_t* bar = ui_box(bars_);
        lv_obj_set_flex_grow(bar, 1);
        lv_obj_set_height(bar, LV_PCT(heights[i]));
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(i + 1 == heights.size() ? UI_ACCENT : UI_BAR), 0);
    }
}

} // namespace app::ui
