#ifndef APP_CPP_UI_TILES_COMPACTTILE_H
#define APP_CPP_UI_TILES_COMPACTTILE_H

#include "../details/EntityDetail.h"
#include "TileWidget.h"

#include <memory>
#include <optional>
#include <string>

namespace app::ui {

// The small tile for HA's small cards (DashboardLayout's CellKind::Compact,
// a third of a tile high, often half a column wide): icon, name and state,
// left aligned. Tap and hold do what they do on the domain's own tile:
// light and media player: tap the quick action, hold the detail page;
// cover: tap the detail page, hold open / close; climate and alarm panel:
// tap the detail page; lock: tap locks, hold unlocks; the simple domains:
// tap the quick action (toggle, press). Others only show their state.
class CompactTile final : public TileWidget {
public:
    using TileWidget::TileWidget;

    struct Facts { // the entity facts Look depends on
        std::string domain, state, device_class, unit;
        std::optional<double> brightness, position, current_temperature, temperature;
        bool available = true;
    };
    struct Look {
        const char* icon = nullptr;
        int look = 0; // ui_look_t
        std::string state_text;
    };
    // What the tile shows (pure; host-tested).
    static Look look_for(const Facts& f);
    // The quick action's service ("" without one; never unlock).
    static std::string tap_service(const std::string& domain, const std::string& state);
    // Domains whose tap opens the detail page (the quick action moves to hold).
    static bool tap_opens_detail(const std::string& domain);

    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;
    void on_hold() override;

private:
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    std::unique_ptr<EntityDetail> detail_;

    void quick_action();
    void open_detail();
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_COMPACTTILE_H
