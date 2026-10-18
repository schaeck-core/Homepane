#ifndef APP_CPP_UI_TILES_CAMERATILE_H
#define APP_CPP_UI_TILES_CAMERATILE_H

#include "TileWidget.h"
#include "../../media/ImageDecoder.h"

#include "lvgl/lvgl.h"

#include <memory>

namespace app::ui {

// A camera as a tile: its snapshot (entity_picture, via ImageLoader),
// refreshed every 10 s; a tap opens it large, refreshed every 3 s.
class CameraTile final : public TileWidget {
public:
    using TileWidget::TileWidget;
    ~CameraTile() override;

    void shutdown_ui() override;
    void load_tile();
    void load_large();
    void on_large_closed();

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;

private:
    struct View {
        lv_obj_t* box = nullptr;   // the rounded frame
        lv_obj_t* image = nullptr;
        lv_obj_t* note = nullptr;  // "…" / error over the frame
        lv_image_dsc_t dsc{};
        std::shared_ptr<app::media::DecodedImage> picture;
        app::core::CancellationSource pending;
        lv_timer_t* timer = nullptr;
        bool loading = false;
    };
    void load(View& v, bool cover);
    void show(View& v, std::shared_ptr<app::media::DecodedImage> picture, const std::string& error);
    void stop(View& v);

    View tile_view_;
    View large_view_;
    lv_obj_t* large_root_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_CAMERATILE_H
