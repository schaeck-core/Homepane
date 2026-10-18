#ifndef APP_CPP_UI_TILES_COVERTILE_H
#define APP_CPP_UI_TILES_COVERTILE_H

#include "../details/EntityDetail.h"
#include "TileWidget.h"

#include <memory>

namespace app::ui {

// The cover tile of the design guide: a window (or garage door) whose
// shade shows the position, open / close buttons below; while the cover
// moves, the button of that direction becomes stop.
class CoverTile final : public TileWidget {
public:
    using TileWidget::TileWidget;

    // Share of the window the shade covers, 0..100 (pure; host-tested).
    static int shade_percent(const std::string& state, std::optional<double> position);
    void button_pressed(int index); // 0 = up, 1 = down
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;
    void on_hold() override { on_tap(); }
    void on_command_failed() override;

private:
    bool garage() const;
    void fill_slats(int32_t height);

    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* pane_ = nullptr;
    lv_obj_t* shade_ = nullptr;
    lv_obj_t* bars_[2] = {nullptr, nullptr};
    bool shown_garage_ = false;
    lv_obj_t* buttons_[2] = {nullptr, nullptr};
    int32_t slats_for_height_ = -1;
    std::unique_ptr<CoverDetail> detail_;
    CoverDetail::State detail_state() const;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_COVERTILE_H
