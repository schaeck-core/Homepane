#ifndef APP_CPP_UI_TILES_CHARTTILE_H
#define APP_CPP_UI_TILES_CHARTTILE_H

#include "../../home_assistant/QueryTypes.h"
#include "../Widget.h"

#include <optional>
#include <string>
#include <vector>

namespace app::ui {

// The chart tile of the design guide (statistics-graph card): title, the
// current value of the first entity, the recent hourly means as bars (the
// last one, "now", in sand); "Noch keine Daten" instead of an empty chart.
class ChartTile final : public Widget {
public:
    static constexpr size_t kBars = 24;

    ChartTile(app::home_assistant::HaInstanceId instance, app::home_assistant::StatisticsQuery query, std::string title);

    // Bar heights 0..100 for the last kBars means, always kBars of them
    // (missing hours first, as minimal bars); empty without means (pure; host-tested).
    static std::vector<int> bar_heights(const std::vector<double>& means);
    // 1240 W -> {"1,24", "kW"}; other values with German decimals.
    static std::pair<std::string, std::string> value_text(const std::string& state, const std::string& unit);

    std::vector<DataRequirement> requirements() const override;
    void create(lv_obj_t* parent) override;
    void on_data(const DataUpdate& update) override;

private:
    void render_bars();

    app::home_assistant::HaInstanceId instance_;
    app::home_assistant::StatisticsQuery query_;
    std::string title_;
    std::vector<double> means_;
    bool has_statistics_ = false;

    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* value_label_ = nullptr;
    lv_obj_t* unit_label_ = nullptr;
    lv_obj_t* bars_ = nullptr;
    lv_obj_t* empty_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_CHARTTILE_H
