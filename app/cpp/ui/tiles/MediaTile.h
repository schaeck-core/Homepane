#ifndef APP_CPP_UI_TILES_MEDIATILE_H
#define APP_CPP_UI_TILES_MEDIATILE_H

#include "../details/EntityDetail.h"
#include "TileWidget.h"

#include <memory>

namespace app::ui {

// The media tile of the design guide: artwork area, name and what plays,
// a round play/pause button (power when off). Tap: play/pause, double
// tap: next track.
class MediaTile final : public TileWidget {
public:
    using TileWidget::TileWidget;

    // "Spielt · Blue in Green" (pure; host-tested).
    static std::string status_text(const std::string& state, const std::string& title, const std::string& artist);
    void play_button_pressed();
    void open_detail();
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;
    bool has_double_tap() const override { return true; }
    void on_double_tap() override;
    void on_hold() override { open_detail(); }

private:
    bool is_tv() const;

    lv_obj_t* art_icon_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* play_ = nullptr;
    std::unique_ptr<MediaDetail> detail_;
    MediaDetail::State detail_state() const;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_MEDIATILE_H
