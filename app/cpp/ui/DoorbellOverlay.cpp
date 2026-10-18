#include "DoorbellOverlay.h"

#include "../media/ImageDsc.h"
#include "../media/ImageLoader.h"
#include "tiles/TileWidget.h"
#include "../../i18n.h"

#include "../../ui_kit.h"

#include <cstdio>

namespace app::ui {

namespace {
constexpr uint32_t kAutoCloseMs = 120000;
constexpr uint32_t kCloseAfterOpenMs = 5000;
constexpr uint32_t kPictureGapMs = 300; // between two camera pictures

DoorbellOverlay* self_of(lv_event_t* e) { return static_cast<DoorbellOverlay*>(lv_event_get_user_data(e)); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_close_cb(lv_event_t* e) { self_of(e)->close(); }
void raw_hold_cb(lv_event_t* e) { self_of(e)->on_hold(lv_event_get_code(e) == LV_EVENT_PRESSED); }

lv_obj_t* big_button(lv_obj_t* parent, uint32_t bg)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_style_radius(b, UI_R_TILE, 0);
    lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 8, 0);
    return b;
}
} // namespace

DoorbellOverlay::~DoorbellOverlay()
{
    pending_.cancel();
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

void DoorbellOverlay::show(const std::string& camera, OpenFn open)
{
    rang_at_ = lv_tick_get();
    lv_display_trigger_activity(nullptr); // wakes the display
    if (root_) {
        if (!done_) set_texts(tr(TR_DOORBELL_RINGING), tr(TR_TIME_JUST_NOW));
        return;
    }
    camera_ = camera;
    open_ = std::move(open);
    progress_ = 0;
    opening_ = done_ = false;
    closes_at_ = 0;

    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE); // nothing below reacts
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(root_, UI_EDGE, 0);
    lv_obj_set_style_pad_row(root_, UI_EDGE, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);

    // the camera picture with the "Live" badge
    frame_ = ui_box(root_);
    lv_obj_set_width(frame_, LV_PCT(100));
    lv_obj_set_flex_grow(frame_, 1);
    lv_obj_set_style_radius(frame_, UI_R_TILE, 0);
    lv_obj_set_style_clip_corner(frame_, true, 0);
    lv_obj_set_style_bg_opa(frame_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame_, lv_color_hex(UI_SURFACE), 0);
    image_ = lv_image_create(frame_);
    lv_obj_center(image_);
    placeholder_ = ui_label(frame_, UI_ICON_TILE, UI_MUTED, LI_CAMERA);
    lv_obj_center(placeholder_);
    if (!camera_.empty()) {
        lv_obj_t* badge = ui_box(frame_);
        lv_obj_set_size(badge, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_radius(badge, 22, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(UI_BG), 0);
        lv_obj_set_style_pad_hor(badge, 16, 0);
        lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(badge, 8, 0);
        lv_obj_t* dot = ui_box(badge);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(UI_WARN), 0);
        ui_label(badge, UI_FONT_LABEL, UI_TEXT, tr(TR_DOORBELL_LIVE));
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 16, 16);
    }

    lv_obj_t* texts = ui_box(root_);
    lv_obj_set_size(texts, LV_PCT(100), 64);
    lv_obj_set_style_pad_hor(texts, 4, 0);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(texts, 2, 0);
    title_ = ui_label_line(texts, UI_FONT_TITLE, UI_TEXT, "");
    sub_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");

    lv_obj_t* row = ui_box(root_);
    lv_obj_set_size(row, LV_PCT(100), 112);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_t* close_btn = big_button(row, UI_SURFACE);
    lv_obj_set_flex_grow(close_btn, 10);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    ui_label(close_btn, UI_ICON_BUTTON, UI_TEXT, LI_X);
    ui_label(close_btn, UI_FONT_LABEL, UI_TEXT, tr(TR_DOORBELL_CLOSE));
    lv_obj_add_event_cb(close_btn, raw_close_cb, LV_EVENT_CLICKED, this);

    if (open_) {
        open_btn_ = big_button(row, UI_SURFACE2);
        lv_obj_set_flex_grow(open_btn_, 14);
        open_fill_ = ui_box(open_btn_);
        lv_obj_add_flag(open_fill_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(open_fill_, 0, LV_PCT(100));
        lv_obj_set_style_bg_opa(open_fill_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(open_fill_, lv_color_hex(UI_ACCENT), 0);
        open_icon_ = ui_label(open_btn_, UI_ICON_BUTTON, UI_TEXT, LI_DOOR_OPEN);
        open_label_ = ui_label(open_btn_, UI_FONT_LABEL, UI_TEXT, tr(TR_DOORBELL_HOLD_TO_OPEN));
        lv_obj_add_event_cb(open_btn_, raw_hold_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(open_btn_, raw_hold_cb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(open_btn_, raw_hold_cb, LV_EVENT_PRESS_LOST, this);
    }

    set_texts(tr(TR_DOORBELL_RINGING), tr(TR_TIME_JUST_NOW));
    frames_ = 0;
    fps_since_ = lv_tick_get();
    tick_ = lv_timer_create([](lv_timer_t* t) { static_cast<DoorbellOverlay*>(lv_timer_get_user_data(t))->on_tick(); },
                            1000, this);
    start_stream();
}

void DoorbellOverlay::close()
{
    if (root_) lv_obj_delete(root_); // -> on_root_deleted()
}

void DoorbellOverlay::on_root_deleted()
{
    for (lv_timer_t** t : {&tick_, &hold_timer_, &next_picture_}) {
        if (*t) lv_timer_delete(*t);
        *t = nullptr;
    }
    pending_.cancel();
    loading_ = streaming_ = false;
    if (image_) lv_image_cache_drop(&dsc_);
    picture_.reset();
    root_ = frame_ = image_ = placeholder_ = title_ = sub_ = nullptr;
    open_btn_ = open_fill_ = open_label_ = open_icon_ = nullptr;
}

void DoorbellOverlay::set_texts(const char* title, const std::string& sub)
{
    if (!title_) return;
    lv_label_set_text(title_, title);
    lv_label_set_text(sub_, sub.c_str());
}

void DoorbellOverlay::on_tick()
{
    lv_display_trigger_activity(nullptr); // the display stays on while it rings
    const uint32_t now = lv_tick_get();
    if (now - fps_since_ >= 5000) {
        LV_LOG_USER("doorbell: %.1f pictures/s (%s)", frames_ * 1000.0 / (now - fps_since_),
                    streaming_ ? "stream" : "stills");
        printf("doorbell: %.1f pictures/s (%s)\n", frames_ * 1000.0 / (now - fps_since_), streaming_ ? "stream" : "stills");
        frames_ = 0;
        fps_since_ = now;
    }
    if ((closes_at_ && now >= closes_at_) || (!closes_at_ && now - rang_at_ >= kAutoCloseMs)) {
        close();
        return;
    }
    if (done_) return;
    const uint32_t s = (now - rang_at_) / 1000;
    set_texts(tr(TR_DOORBELL_RINGING), s < 2 ? std::string(tr(TR_TIME_JUST_NOW))
                                       : s < 60 ? trf(TR_DOORBELL_SECONDS_AGO, {std::to_string(s)})
                                                : trf(TR_TIME_AGO, {format_duration(s)}));
}

void DoorbellOverlay::on_hold(bool pressed)
{
    if (!open_btn_ || opening_ || done_) return;
    if (pressed) {
        if (!hold_timer_) {
            hold_timer_ = lv_timer_create(
                [](lv_timer_t* t) { static_cast<DoorbellOverlay*>(lv_timer_get_user_data(t))->on_hold_step(); }, 50,
                this);
        }
        lv_label_set_text(open_label_, tr(TR_DOORBELL_HOLDING));
        return;
    }
    // released early: back to the start
    if (hold_timer_) lv_timer_delete(hold_timer_);
    hold_timer_ = nullptr;
    progress_ = 0;
    lv_obj_set_width(open_fill_, 0);
    lv_label_set_text(open_label_, tr(TR_DOORBELL_HOLD_TO_OPEN));
    lv_obj_set_style_text_color(open_label_, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(open_icon_, lv_color_hex(UI_TEXT), 0);
}

void DoorbellOverlay::on_hold_step()
{
    progress_ = progress_ + 5 > 100 ? 100 : progress_ + 5; // a full second of holding
    lv_obj_set_width(open_fill_, lv_pct(progress_));
    const uint32_t fg = progress_ > 55 ? UI_BG : UI_TEXT;
    lv_obj_set_style_text_color(open_label_, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(open_icon_, lv_color_hex(fg), 0);
    if (progress_ < 100) return;
    lv_timer_delete(hold_timer_);
    hold_timer_ = nullptr;
    opening_ = true;
    if (open_) open_([this](bool ok) { opened(ok); });
}

void DoorbellOverlay::opened(bool ok)
{
    opening_ = false;
    if (!root_) return;
    if (!ok) {
        ui_toast(tr(TR_DOORBELL_OPEN_FAILED));
        on_hold(false);
        return;
    }
    done_ = true;
    closes_at_ = lv_tick_get() + kCloseAfterOpenMs;
    lv_label_set_text(open_label_, tr(TR_DOORBELL_OPENED_SHORT));
    set_texts(tr(TR_DOORBELL_OPENED), tr(TR_DOORBELL_CLOSES_SOON));
}

void DoorbellOverlay::show_picture(std::shared_ptr<app::media::DecodedImage> picture)
{
    if (!image_ || !picture) return;
    ++frames_;
    lv_image_cache_drop(&dsc_);
    picture_ = std::move(picture);
    app::media::set_image_dsc(dsc_, *picture_);
    lv_image_set_src(image_, &dsc_);
    lv_obj_center(image_);
    lv_obj_add_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
}

void DoorbellOverlay::start_stream()
{
    app::media::ImageLoader* loader = app::media::image_loader();
    if (!loader || !frame_ || camera_.empty()) return;
    lv_obj_update_layout(frame_);
    const int w = lv_obj_get_content_width(frame_), h = lv_obj_get_content_height(frame_);
    if (w <= 0 || h <= 0) return;
    streaming_ = true;
    pending_.cancel();
    pending_ = app::core::CancellationSource();
    // HA serves every camera as MJPEG here (its own stream or stills it repeats)
    loader->stream(
        "/api/camera_proxy_stream/" + camera_, w, h, true, pending_.token(),
        [this](std::shared_ptr<app::media::DecodedImage> picture, const std::string&) { show_picture(std::move(picture)); },
        [this](const std::string& error) {
            // no stream: single pictures instead
            LV_LOG_WARN("doorbell: camera stream ended (%s), using stills", error.c_str());
            streaming_ = false;
            load_picture();
        });
}

void DoorbellOverlay::load_picture()
{
    if (next_picture_) {
        lv_timer_delete(next_picture_);
        next_picture_ = nullptr;
    }
    app::media::ImageLoader* loader = app::media::image_loader();
    if (!loader || !frame_ || loading_ || camera_.empty()) return;
    lv_obj_update_layout(frame_);
    const int w = lv_obj_get_content_width(frame_), h = lv_obj_get_content_height(frame_);
    if (w <= 0 || h <= 0) return;
    loading_ = true;
    pending_.cancel();
    pending_ = app::core::CancellationSource();
    loader->load("/api/camera_proxy/" + camera_, w, h, true, pending_.token(),
                 [this](std::shared_ptr<app::media::DecodedImage> picture, const std::string&) {
                     loading_ = false;
                     if (!image_) return;
                     show_picture(std::move(picture));
                     // the next picture right after this one
                     next_picture_ = lv_timer_create(
                         [](lv_timer_t* t) {
                             auto* self = static_cast<DoorbellOverlay*>(lv_timer_get_user_data(t));
                             lv_timer_delete(t);
                             self->next_picture_ = nullptr;
                             self->load_picture();
                         },
                         kPictureGapMs, this);
                 });
}

} // namespace app::ui
