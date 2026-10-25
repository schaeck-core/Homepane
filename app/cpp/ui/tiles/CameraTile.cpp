#include "CameraTile.h"

#include "../../media/ImageDsc.h"
#include "../../../i18n.h"
#include "../../media/ImageLoader.h"

#include "../../../ui_kit.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h" // lv_image_cache_drop(): private API since LVGL 9.4

namespace app::ui {

namespace {
constexpr uint32_t kTileRefreshMs = 10000;
constexpr uint32_t kLargeRefreshMs = 3000;

lv_obj_t* frame(lv_obj_t* parent)
{
    lv_obj_t* box = ui_box(parent);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_SURFACE2), 0);
    return box;
}

void large_deleted_cb(lv_event_t* e) { static_cast<CameraTile*>(lv_event_get_user_data(e))->on_large_closed(); }
} // namespace

CameraTile::~CameraTile()
{
    tile_view_.pending.cancel();
    large_view_.pending.cancel();
}

void CameraTile::build(lv_obj_t* tile)
{
    tile_view_.box = frame(tile);
    lv_obj_set_width(tile_view_.box, LV_PCT(100));
    lv_obj_set_flex_grow(tile_view_.box, 1);
    tile_view_.image = lv_image_create(tile_view_.box);
    lv_obj_center(tile_view_.image);
    tile_view_.note = ui_label(tile_view_.box, UI_ICON_TILE, UI_TEXT3, LI_CAMERA);
    lv_obj_center(tile_view_.note);

    lv_obj_t* bottom = ui_box(tile);
    lv_obj_set_size(bottom, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(bottom, 10, 0);
    name_label_ = ui_label_line(bottom, UI_FONT_NAME, UI_TEXT, "");
    state_label_ = ui_label_line(bottom, UI_FONT_LABEL, UI_TEXT2, "");

    tile_view_.timer = lv_timer_create(
        [](lv_timer_t* t) { static_cast<CameraTile*>(lv_timer_get_user_data(t))->load_tile(); }, kTileRefreshMs, this);
    lv_timer_ready(tile_view_.timer); // the first picture right away
}

void CameraTile::update()
{
    if (!name_label_) return;
    lv_label_set_text(name_label_, name().c_str());
    const std::string& s = state();
    lv_label_set_text(state_label_, !available() ? tr(TR_STATE_UNAVAILABLE)
                                    : tr(s == "recording" ? TR_CAMERA_RECORDING
                                         : s == "streaming" ? TR_CAMERA_STREAMING : TR_CAMERA_IDLE));
}

void CameraTile::load_tile() { load(tile_view_, true); }
void CameraTile::load_large() { load(large_view_, false); }

void CameraTile::load(View& v, bool cover)
{
    app::media::ImageLoader* loader = app::media::image_loader();
    if (!loader || !v.box || v.loading || !loaded() || !available()) return;
    const std::string picture = attr("entity_picture");
    if (picture.empty()) return;
    lv_obj_update_layout(v.box);
    const int w = lv_obj_get_content_width(v.box), h = lv_obj_get_content_height(v.box);
    if (w <= 0 || h <= 0) return;
    v.loading = true;
    v.pending.cancel();
    v.pending = app::core::CancellationSource();
    View* view = &v;
    loader->load(picture, w, h, cover, v.pending.token(),
                 [this, view](std::shared_ptr<app::media::DecodedImage> image, const std::string& error) {
                     view->loading = false;
                     show(*view, std::move(image), error);
                 });
}

void CameraTile::show(View& v, std::shared_ptr<app::media::DecodedImage> picture, const std::string& error)
{
    if (!v.image) return;
    if (!picture) {
        // keep the last picture; without one, say why
        if (!v.picture) {
            LV_LOG_WARN("camera %s: %s", entity_id().c_str(), error.c_str());
            lv_label_set_text(v.note, tr(TR_CAMERA_NO_PICTURE));
            lv_obj_set_style_text_font(v.note, UI_FONT_LABEL, 0);
        }
        return;
    }
    lv_image_cache_drop(&v.dsc); // same descriptor, new pixels
    v.picture = std::move(picture);
    app::media::set_image_dsc(v.dsc, *v.picture);
    lv_image_set_src(v.image, &v.dsc);
    lv_obj_center(v.image);
    lv_obj_add_flag(v.note, LV_OBJ_FLAG_HIDDEN);
}

void CameraTile::stop(View& v)
{
    v.pending.cancel();
    v.loading = false;
    if (v.timer) {
        lv_timer_delete(v.timer);
        v.timer = nullptr;
    }
    if (v.image) lv_image_cache_drop(&v.dsc);
    v.box = v.image = v.note = nullptr;
    v.picture.reset();
}

void CameraTile::on_tap()
{
    if (large_root_ || !loaded() || !available()) return;
    ui_detail_t d = ui_detail_open(name().c_str(), nullptr);
    large_root_ = d.root;
    lv_obj_add_event_cb(large_root_, large_deleted_cb, LV_EVENT_DELETE, this);
    large_view_.box = frame(d.content);
    lv_obj_set_style_bg_color(large_view_.box, lv_color_hex(UI_BG), 0); // the picture alone on black
    lv_obj_set_width(large_view_.box, LV_PCT(100));
    lv_obj_set_flex_grow(large_view_.box, 1);
    large_view_.image = lv_image_create(large_view_.box);
    lv_obj_center(large_view_.image);
    large_view_.note = ui_label(large_view_.box, UI_FONT_LABEL, UI_TEXT2, "…");
    lv_obj_center(large_view_.note);
    large_view_.timer = lv_timer_create(
        [](lv_timer_t* t) { static_cast<CameraTile*>(lv_timer_get_user_data(t))->load_large(); }, kLargeRefreshMs,
        this);
    lv_timer_ready(large_view_.timer);
}

void CameraTile::on_large_closed()
{
    large_root_ = nullptr;
    stop(large_view_);
}

void CameraTile::shutdown_ui()
{
    if (large_root_) {
        lv_obj_t* root = large_root_;
        lv_obj_remove_event_cb_with_user_data(root, large_deleted_cb, this);
        large_root_ = nullptr;
        stop(large_view_);
        lv_obj_delete(root); // the page lives on the screen, not under the tile
    }
    stop(tile_view_);
    name_label_ = state_label_ = nullptr;
    TileWidget::shutdown_ui();
}

} // namespace app::ui
