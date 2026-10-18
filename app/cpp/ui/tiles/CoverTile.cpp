#include "CoverTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cmath>

namespace app::ui {

namespace {
constexpr uint32_t kFrame = 0x3A3833;
constexpr uint32_t kGlass = 0x10161A;
constexpr int32_t kWindowW = 120;
constexpr int32_t kBorder = 7;

void raw_button_cb(lv_event_t* e)
{
    auto* self = static_cast<CoverTile*>(lv_event_get_user_data(e));
    self->button_pressed(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}
} // namespace

int CoverTile::shade_percent(const std::string& state, std::optional<double> position)
{
    double open = position ? *position : (state == "closed" ? 0.0 : 100.0);
    open = std::fmax(0.0, std::fmin(100.0, open));
    return 100 - static_cast<int>(std::lround(open));
}

bool CoverTile::garage() const
{
    const std::string c = attr("device_class");
    return c == "garage" || c == "gate";
}

void CoverTile::build(lv_obj_t* tile)
{
    lv_obj_t* body = ui_box(tile);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, UI_GAP, 0);
    name_label_ = ui_label_line(body, UI_FONT_NAME, UI_TEXT, "");

    // window: frame on three sides, glass, cross bars, shade from the top; sill below
    lv_obj_t* window = ui_box(body);
    lv_obj_set_width(window, kWindowW + 16); // the sill is 8 px wider on each side
    lv_obj_set_flex_grow(window, 1);
    lv_obj_set_flex_flow(window, LV_FLEX_FLOW_COLUMN);
    pane_ = ui_box(window);
    lv_obj_set_width(pane_, kWindowW);
    lv_obj_set_style_margin_left(pane_, 8, 0);
    lv_obj_set_flex_grow(pane_, 1);
    lv_obj_set_style_bg_color(pane_, lv_color_hex(kGlass), 0);
    lv_obj_set_style_bg_opa(pane_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pane_, lv_color_hex(kFrame), 0);
    lv_obj_set_style_border_width(pane_, kBorder, 0);
    lv_obj_set_style_border_side(pane_, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT |
                                                                      LV_BORDER_SIDE_RIGHT), 0);
    // cross bars of a window (hidden for garage doors)
    bars_[0] = ui_box(pane_);
    lv_obj_set_size(bars_[0], 5, LV_PCT(100));
    lv_obj_align(bars_[0], LV_ALIGN_TOP_MID, 0, 0);
    bars_[1] = ui_box(pane_);
    lv_obj_set_size(bars_[1], LV_PCT(100), 5);
    lv_obj_align(bars_[1], LV_ALIGN_LEFT_MID, 0, -8); // a bit above the middle, like a real window
    for (lv_obj_t* bar : bars_) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(kFrame), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    shade_ = ui_box(pane_);
    lv_obj_set_width(shade_, LV_PCT(100));
    lv_obj_align(shade_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(shade_, LV_OPA_COVER, 0);
    lv_obj_t* sill = ui_box(window);
    lv_obj_set_size(sill, LV_PCT(100), 10);
    lv_obj_set_style_radius(sill, 2, 0);
    lv_obj_set_style_bg_color(sill, lv_color_hex(kFrame), 0);
    lv_obj_set_style_bg_opa(sill, LV_OPA_COVER, 0);

    lv_obj_t* row = ui_button_row(tile);
    for (int i = 0; i < 2; ++i) {
        buttons_[i] = ui_tile_button(row, i == 0 ? LI_CHEVRON_UP : LI_CHEVRON_DOWN, nullptr, false);
        lv_obj_add_event_cb(buttons_[i], raw_button_cb, LV_EVENT_CLICKED, this);
    }
}

// Slat lines over the shade, every 14 px (garage: 29 px).
void CoverTile::fill_slats(int32_t height)
{
    if (height == slats_for_height_) return;
    slats_for_height_ = height;
    lv_obj_clean(shade_);
    const int32_t pitch = garage() ? 29 : 14;
    const int32_t line = garage() ? 3 : 2;
    for (int32_t y = pitch - line; y < height; y += pitch) {
        lv_obj_t* l = ui_box(shade_);
        lv_obj_set_size(l, LV_PCT(100), line);
        lv_obj_set_y(l, y);
        lv_obj_set_style_bg_color(l, lv_color_hex(garage() ? 0x4A4741 : UI_MUTED), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
}

void CoverTile::update()
{
    if (!shade_) return;
    lv_label_set_text(name_label_, name().c_str());
    const bool is_garage = garage();
    if (is_garage != shown_garage_) {
        shown_garage_ = is_garage;
        slats_for_height_ = -1;
        for (lv_obj_t* bar : bars_) {
            if (is_garage) lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_set_style_bg_color(shade_, lv_color_hex(is_garage ? 0x6E6A63 : UI_TEXT3), 0);
    const int percent = loaded() ? shade_percent(state(), attr_number("current_position")) : 0;
    lv_obj_update_layout(pane_);
    const int32_t inner = lv_obj_get_content_height(pane_);
    const int32_t h = inner * percent / 100;
    lv_obj_set_height(shade_, h);
    fill_slats(h);

    const std::string& s = state();
    const bool opening = s == "opening", closing = s == "closing";
    ui_tile_button_set(buttons_[0], opening ? LI_SQUARE : LI_CHEVRON_UP, nullptr, opening);
    ui_tile_button_set(buttons_[1], closing ? LI_SQUARE : LI_CHEVRON_DOWN, nullptr, closing);
}

void CoverTile::button_pressed(int index)
{
    if (!loaded() || !available()) return;
    const std::string& s = state();
    if ((index == 0 && s == "opening") || (index == 1 && s == "closing")) {
        call("stop_cover");
        return;
    }
    call(index == 0 ? "open_cover" : "close_cover");
}

CoverDetail::State CoverTile::detail_state() const { return cover_detail_state(entity(), name(), state()); }

void CoverTile::on_tap()
{
    if (!loaded() || !available()) return;
    if (!detail_) detail_ = std::make_unique<CoverDetail>([this](const std::string& service, const std::string& json) {
        call(service, json);
    });
    detail_->open(detail_state());
}

void CoverTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    if (detail_ && detail_->is_open() && u.entity_state) detail_->update(detail_state());
}

void CoverTile::shutdown_ui()
{
    detail_.reset(); // the page lives on the screen, not under the tile
    TileWidget::shutdown_ui();
}

void CoverTile::on_command_failed() { ui_toast(trf(TR_DASHBOARD_COMMAND_FAILED, {name()}).c_str()); }

} // namespace app::ui
