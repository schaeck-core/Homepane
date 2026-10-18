#include "MediaTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

namespace app::ui {

namespace {
void raw_play_cb(lv_event_t* e) { static_cast<MediaTile*>(lv_event_get_user_data(e))->play_button_pressed(); }
void raw_art_cb(lv_event_t* e) { static_cast<MediaTile*>(lv_event_get_user_data(e))->open_detail(); }
} // namespace

std::string MediaTile::status_text(const std::string& state, const std::string& title, const std::string& artist)
{
    std::string what = title;
    if (!artist.empty()) what += (what.empty() ? "" : " – ") + artist;
    if (state == "playing") return what.empty() ? tr(TR_MEDIA_PLAYING) : std::string(tr(TR_MEDIA_PLAYING)) + " · " + what;
    if (state == "paused") return what.empty() ? tr(TR_MEDIA_PAUSED) : std::string(tr(TR_MEDIA_PAUSED)) + " · " + what;
    if (state == "buffering") return tr(TR_MEDIA_LOADING);
    if (state == "idle" || state == "on") return tr(TR_STATE_READY);
    if (state == "standby") return tr(TR_MEDIA_STANDBY);
    return tr(TR_STATE_OFF);
}

bool MediaTile::is_tv() const { return attr("device_class") == "tv"; }

void MediaTile::build(lv_obj_t* tile)
{
    // artwork area (cover images come later; the placeholder shows the kind of device)
    lv_obj_t* art = ui_box(tile);
    lv_obj_set_width(art, LV_PCT(100));
    lv_obj_set_flex_grow(art, 1);
    lv_obj_set_style_radius(art, 18, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    art_icon_ = ui_label(art, UI_ICON_L, UI_MUTED, LI_MUSIC);
    lv_obj_center(art_icon_);
    // the artwork opens the player (design guide, Main artboard)
    lv_obj_remove_flag(art, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(art, raw_art_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* row = ui_box(tile);
    lv_obj_set_size(row, LV_PCT(100), UI_BUTTON_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_t* texts = ui_box(row);
    // fixed width (cell minus padding, button and gap): labels cut with "..." need it up front
    lv_obj_update_layout(tile);
    const int32_t inner = lv_obj_get_content_width(tile);
    lv_obj_set_size(texts, inner > UI_TOUCH_MIN + 10 ? inner - UI_TOUCH_MIN - 10 : 100, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, 0);
    name_label_ = ui_label_line(texts, UI_FONT_NAME, UI_TEXT, "");
    status_label_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");

    play_ = ui_box(row);
    lv_obj_remove_flag(play_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(play_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(play_, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(play_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(play_, UI_ICON_BUTTON, 0x000000, LI_PLAY));
    lv_obj_add_event_cb(play_, raw_play_cb, LV_EVENT_CLICKED, this);
}

void MediaTile::update()
{
    if (!play_) return;
    const std::string& s = state();
    lv_label_set_text(art_icon_, is_tv() ? LI_TV : LI_MUSIC);
    lv_label_set_text(name_label_, name().c_str());
    lv_label_set_text(status_label_,
                      !loaded() ? "" : !available() ? tr(TR_STATE_UNAVAILABLE)
                                                    : status_text(s, attr("media_title"), attr("media_artist")).c_str());
    const bool off = s == "off" || s == "standby" || !available();
    lv_obj_t* icon = lv_obj_get_child(play_, 0);
    lv_label_set_text(icon, off ? LI_POWER : s == "playing" ? LI_PAUSE : LI_PLAY);
    lv_obj_set_style_bg_color(play_, lv_color_hex(off ? UI_SURFACE2 : UI_TEXT), 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(off ? UI_TEXT : 0x000000), 0);
}

void MediaTile::play_button_pressed()
{
    if (!loaded() || !available()) return;
    const std::string& s = state();
    if (s == "off" || s == "standby") {
        expect_state("on");
        call("turn_on");
        return;
    }
    expect_state(s == "playing" ? "paused" : "playing");
    call("media_play_pause");
}

void MediaTile::on_tap() { play_button_pressed(); }

MediaDetail::State MediaTile::detail_state() const { return media_detail_state(entity(), name(), state()); }

void MediaTile::open_detail()
{
    if (!loaded() || !available()) return;
    if (!detail_) detail_ = std::make_unique<MediaDetail>([this](const std::string& service, const std::string& json) {
        call(service, json);
    });
    detail_->open(detail_state());
}

void MediaTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    if (detail_ && detail_->is_open() && u.entity_state) detail_->update(detail_state());
}

void MediaTile::shutdown_ui()
{
    detail_.reset(); // the page lives on the screen, not under the tile
    TileWidget::shutdown_ui();
}

void MediaTile::on_double_tap()
{
    if (!loaded() || !available() || state() == "off") return;
    call("media_next_track");
    ui_toast(tr(TR_TILE_NEXT_TRACK));
}

} // namespace app::ui
