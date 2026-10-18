#include "CoverDetail.h"

#include "../tiles/TileWidget.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cmath>
#include <cstdio>

namespace app::ui {

namespace {
constexpr uint32_t kFrame = 0x3A3833;
constexpr uint32_t kGlass = 0x10161A;
struct Preset {
    tr_key_t label;
    int position;
};
constexpr Preset kPresets[] = {{TR_COVER_PRESET_CLOSED, 0},
                               {TR_COVER_PRESET_PRIVACY, 30},
                               {TR_COVER_PRESET_HALF, 50},
                               {TR_COVER_PRESET_OPEN, 100}};

CoverDetail* self_of(lv_event_t* e) { return static_cast<CoverDetail*>(lv_event_get_user_data(e)); }
int index_of(lv_event_t* e) { return static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_window_cb(lv_event_t* e) { self_of(e)->on_window(lv_event_get_code(e) == LV_EVENT_RELEASED); }
void raw_button_cb(lv_event_t* e) { self_of(e)->on_button(index_of(e)); }
void raw_preset_cb(lv_event_t* e) { self_of(e)->on_preset(index_of(e)); }
void raw_tilt_cb(lv_event_t* e) { self_of(e)->on_tilt(lv_event_get_code(e) == LV_EVENT_RELEASED); }

lv_obj_t* key(lv_obj_t* row, int32_t radius)
{
    lv_obj_t* b = ui_box(row);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    return b;
}
} // namespace

std::string CoverDetail::status_text(const State& s)
{
    if (s.state == "opening") return tr(TR_COVER_OPENING);
    if (s.state == "closing") return tr(TR_COVER_CLOSING);
    const double p = s.position.value_or(s.state == "closed" ? 0 : 100);
    if (p <= 0) return tr(TR_STATE_CLOSED);
    if (p >= 100) return tr(TR_COVER_FULLY_OPEN);
    return tr(TR_STATE_OPEN);
}

CoverDetail::CoverDetail(Send send) : send_(std::move(send)) {}

CoverDetail::~CoverDetail()
{
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
}

void CoverDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    build();
    refresh();
}

void CoverDetail::update(const State& state)
{
    if (!root_ || dragging_) return;
    state_ = state;
    refresh();
}

void CoverDetail::on_root_deleted() { root_ = nullptr; }

void CoverDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), nullptr);
    root_ = d.root;
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    lv_obj_t* c = d.content;
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, 16, 0);

    // left: the window to drag
    lv_obj_t* panel = ui_box(c);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(panel, 248, LV_PCT(100));
    lv_obj_set_style_radius(panel, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(panel, 24, 0);
    lv_obj_set_style_pad_hor(panel, 20, 0);
    lv_obj_set_style_pad_bottom(panel, 20, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    pane_ = ui_box(panel);
    lv_obj_remove_flag(pane_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(pane_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pane_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_width(pane_, LV_PCT(100));
    lv_obj_set_flex_grow(pane_, 1);
    lv_obj_set_style_bg_color(pane_, lv_color_hex(kGlass), 0);
    lv_obj_set_style_bg_opa(pane_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pane_, lv_color_hex(kFrame), 0);
    lv_obj_set_style_border_width(pane_, 10, 0);
    lv_obj_set_style_border_side(pane_, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT |
                                                                      LV_BORDER_SIDE_RIGHT), 0);
    if (!state_.garage) {
        lv_obj_t* v = ui_box(pane_);
        lv_obj_set_size(v, 6, LV_PCT(100));
        lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* h = ui_box(pane_);
        lv_obj_set_size(h, LV_PCT(100), 6);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, 0, -10);
        for (lv_obj_t* bar : {v, h}) {
            lv_obj_set_style_bg_color(bar, lv_color_hex(kFrame), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        }
    }
    shade_ = ui_box(pane_);
    lv_obj_set_width(shade_, LV_PCT(100));
    lv_obj_set_style_bg_color(shade_, lv_color_hex(state_.garage ? 0x6E6A63 : UI_TEXT3), 0);
    lv_obj_set_style_bg_opa(shade_, LV_OPA_COVER, 0);
    if (state_.can_position) {
        for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
            lv_obj_add_event_cb(pane_, raw_window_cb, code, this);
    }
    lv_obj_t* sill = ui_box(panel);
    lv_obj_set_size(sill, LV_PCT(100), 14);
    lv_obj_set_style_radius(sill, 3, 0);
    lv_obj_set_style_bg_color(sill, lv_color_hex(kFrame), 0);
    lv_obj_set_style_bg_opa(sill, LV_OPA_COVER, 0);

    // right: value, buttons, presets, tilt
    lv_obj_t* right = ui_box(c);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 16, 0);
    lv_obj_t* head = ui_box(right);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(head, 4, 0);
    lv_obj_set_style_pad_left(head, 4, 0);
    lv_obj_t* value_row = ui_box(head);
    lv_obj_set_size(value_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(value_row, 6, 0);
    value_ = ui_label(value_row, UI_FONT_HUGE, UI_TEXT, "");
    lv_obj_set_style_text_letter_space(value_, -3, 0);
    lv_obj_t* pct = ui_label(value_row, UI_FONT_VALUE, UI_TEXT2, "%");
    lv_obj_set_style_pad_bottom(pct, 10, 0);
    status_ = ui_label(head, UI_FONT_BODY, UI_TEXT2, "");

    buttons_ = ui_box(right);
    lv_obj_remove_flag(buttons_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(buttons_, LV_PCT(100), 104);
    lv_obj_set_flex_flow(buttons_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons_, 10, 0);
    const char* const icons[] = {LI_CHEVRON_UP, LI_SQUARE, LI_CHEVRON_DOWN};
    const char* const labels[] = {tr(TR_BUTTON_OPEN), tr(TR_BUTTON_STOP), tr(TR_BUTTON_CLOSE)};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* b = key(buttons_, UI_R_KEY);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 6, 0);
        ui_label(b, UI_ICON_M, UI_TEXT, icons[i]);
        ui_label(b, UI_FONT_NAME, UI_TEXT, labels[i]);
        lv_obj_add_event_cb(b, raw_button_cb, LV_EVENT_CLICKED, this);
    }

    if (state_.can_position) {
        presets_ = ui_box(right);
        lv_obj_remove_flag(presets_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(presets_, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(presets_, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(presets_, 10, 0);
        lv_obj_set_style_pad_row(presets_, 10, 0);
        for (const auto& p : kPresets) {
            lv_obj_t* b = key(presets_, UI_R_KEY);
            lv_obj_set_size(b, (720 - 2 * UI_EDGE - 248 - 16 - 10) / 2, 72);
            lv_obj_set_style_pad_hor(b, 20, 0);
            lv_obj_t* l = ui_label(b, UI_FONT_NAME, UI_TEXT2, tr(p.label));
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
            char v[8];
            std::snprintf(v, sizeof(v), "%d %%", p.position);
            lv_obj_align(ui_label(b, UI_FONT_LABEL, UI_TEXT3, v), LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(b, raw_preset_cb, LV_EVENT_CLICKED, this);
        }
    }

    if (state_.tilt) {
        lv_obj_t* spacer = ui_box(right);
        lv_obj_set_width(spacer, LV_PCT(100));
        lv_obj_set_flex_grow(spacer, 1);
        tilt_ = ui_slider_row(right, nullptr, tr(TR_COVER_TILT));
        lv_obj_add_event_cb(tilt_, raw_tilt_cb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(tilt_, raw_tilt_cb, LV_EVENT_RELEASED, this);
    }
}

void CoverDetail::draw_shade(int position)
{
    lv_obj_update_layout(pane_);
    const int32_t inner = lv_obj_get_content_height(pane_);
    const int32_t h = inner * (100 - position) / 100;
    lv_obj_set_height(shade_, h);
    if (h == slats_height_) return;
    slats_height_ = h;
    lv_obj_clean(shade_);
    const int32_t pitch = state_.garage ? 29 : 21, line = 3;
    for (int32_t y = pitch - line; y < h; y += pitch) {
        lv_obj_t* l = ui_box(shade_);
        lv_obj_set_size(l, LV_PCT(100), line);
        lv_obj_set_y(l, y);
        lv_obj_set_style_bg_color(l, lv_color_hex(state_.garage ? 0x4A4741 : UI_MUTED), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
}

void CoverDetail::refresh()
{
    if (!root_) return;
    const int pos = static_cast<int>(std::lround(state_.position.value_or(state_.state == "closed" ? 0 : 100)));
    draw_shade(pos);
    lv_label_set_text(value_, std::to_string(pos).c_str());
    lv_label_set_text(status_, status_text(state_).c_str());
    const bool opening = state_.state == "opening", closing = state_.state == "closing";
    lv_obj_set_style_bg_color(lv_obj_get_child(buttons_, 0), lv_color_hex(opening ? UI_ACTIVE : UI_SURFACE), 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(buttons_, 2), lv_color_hex(closing ? UI_ACTIVE : UI_SURFACE), 0);
    for (size_t i = 0; presets_ && i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
        lv_obj_t* b = lv_obj_get_child(presets_, static_cast<int32_t>(i));
        const bool sel = pos == kPresets[i].position;
        lv_obj_set_style_bg_color(b, lv_color_hex(sel ? UI_ACTIVE : UI_SURFACE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(sel ? UI_TEXT : UI_TEXT2), 0);
    }
    if (tilt_ && state_.tilt) {
        const int t = static_cast<int>(std::lround(*state_.tilt));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d %%", t);
        ui_slider_row_set(tilt_, t, buf);
    }
}

void CoverDetail::on_window(bool released)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_content_coords(pane_, &a);
    const double h = lv_area_get_height(&a);
    double covered = h > 0 ? (p.y - a.y1) / h : 0; // the shade follows the finger from the top
    covered = std::fmax(0.0, std::fmin(1.0, covered));
    const int pos = 100 - static_cast<int>(std::lround(covered * 100));
    dragging_ = !released;
    state_.position = pos;
    draw_shade(pos);
    lv_label_set_text(value_, std::to_string(pos).c_str());
    if (!released) return;
    send_("set_cover_position", "{\"position\":" + std::to_string(pos) + "}");
    refresh();
}

void CoverDetail::on_button(int index)
{
    if (index == 0) send_("open_cover", "");
    else if (index == 1) send_("stop_cover", "");
    else send_("close_cover", "");
}

void CoverDetail::on_preset(int index)
{
    const int pos = kPresets[index].position;
    state_.position = pos;
    send_("set_cover_position", "{\"position\":" + std::to_string(pos) + "}");
    refresh();
}

void CoverDetail::on_tilt(bool released)
{
    const int t = ui_slider_row_value(tilt_);
    dragging_ = !released;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d %%", t);
    lv_label_set_text(lv_obj_get_child(tilt_, 2), buf);
    if (!released) return;
    state_.tilt = t;
    send_("set_cover_tilt_position", "{\"tilt_position\":" + std::to_string(t) + "}");
}

} // namespace app::ui
