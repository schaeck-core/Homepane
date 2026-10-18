#include "AlarmDetail.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

namespace app::ui {

namespace {
constexpr int kMaxDigits = 6;

AlarmDetail* self_of(lv_event_t* e) { return static_cast<AlarmDetail*>(lv_event_get_user_data(e)); }
int index_of(lv_event_t* e) { return static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_mode_cb(lv_event_t* e) { self_of(e)->on_mode(index_of(e)); }
void raw_key_cb(lv_event_t* e)
{
    // key index = row * 3 + column
    lv_obj_t* key = lv_event_get_current_target_obj(e);
    const int row = static_cast<int>(lv_obj_get_index(lv_obj_get_parent(key)));
    self_of(e)->on_key(row * 3 + static_cast<int>(lv_obj_get_index(key)));
}

lv_obj_t* pressable(lv_obj_t* parent, uint32_t bg)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, UI_R_KEY, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    return b;
}

int mode_label(const std::string& state)
{
    if (state == "armed_home") return TR_ALARM_MODE_HOME;
    if (state == "armed_away") return TR_ALARM_MODE_AWAY;
    if (state == "armed_night") return TR_ALARM_MODE_NIGHT;
    if (state == "armed_vacation") return TR_ALARM_MODE_VACATION;
    return TR_ALARM_ARMED;
}
} // namespace

bool AlarmDetail::is_armed(const std::string& s)
{
    return s.rfind("armed", 0) == 0 || s == "triggered" || s == "pending" || s == "arming";
}

std::vector<AlarmDetail::Mode> AlarmDetail::modes_for(const State& s)
{
    if (is_armed(s.state)) return {{"alarm_disarm", TR_ALARM_DISARM, LI_LOCK_OPEN}};
    std::vector<Mode> out;
    if (s.supported_features & 1) out.push_back({"alarm_arm_home", TR_ALARM_MODE_HOME, LI_HOUSE});
    if (s.supported_features & 2) out.push_back({"alarm_arm_away", TR_ALARM_MODE_AWAY, LI_LOCK});
    if (s.supported_features & 4) out.push_back({"alarm_arm_night", TR_ALARM_MODE_NIGHT, LI_MOON});
    if (s.supported_features & 32) out.push_back({"alarm_arm_vacation", TR_ALARM_MODE_VACATION, LI_PLANE});
    return out;
}

std::string AlarmDetail::service_json(const State& s, const std::string& service, const std::string& code)
{
    const bool needed = s.code_needed && (service == "alarm_disarm" || s.code_arm_required);
    if (!needed || code.empty()) return {};
    return "{\"code\":\"" + code + "\"}"; // digits only
}

AlarmDetail::AlarmDetail(Send send) : send_(std::move(send)) {}

AlarmDetail::~AlarmDetail()
{
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
}

void AlarmDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    code_.clear();
    build();
    refresh();
}

void AlarmDetail::update(const State& state)
{
    if (!root_) return;
    state_ = state;
    refresh();
}

void AlarmDetail::on_root_deleted()
{
    root_ = nullptr;
    code_.clear(); // never keep a code around
}

void AlarmDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), nullptr);
    root_ = d.root;
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    lv_obj_t* c = d.content;
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, 16, 0);

    // left: status, then the modes
    lv_obj_t* left = ui_box(c);
    lv_obj_set_size(left, 280, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 10, 0);
    status_card_ = ui_box(left);
    lv_obj_set_width(status_card_, LV_PCT(100));
    lv_obj_set_flex_grow(status_card_, 1);
    lv_obj_set_style_radius(status_card_, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(status_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(status_card_, 20, 0);
    lv_obj_set_flex_flow(status_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    status_icon_ = ui_label(status_card_, UI_ICON_TILE, UI_TEXT2, LI_SHIELD);
    lv_obj_t* texts = ui_box(status_card_);
    lv_obj_set_size(texts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 4, 0);
    status_title_ = ui_label(texts, UI_FONT_TITLE, UI_TEXT, "");
    status_sub_ = ui_label(texts, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_width(status_sub_, LV_PCT(100));
    lv_label_set_long_mode(status_sub_, LV_LABEL_LONG_WRAP);
    mode_column_ = ui_box(left);
    lv_obj_set_size(mode_column_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mode_column_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mode_column_, 10, 0);

    // right: code dots, keypad
    lv_obj_t* right = ui_box(c);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 10, 0);
    dots_ = ui_box(right);
    lv_obj_set_size(dots_, LV_PCT(100), 64);
    lv_obj_set_style_radius(dots_, UI_R_KEY, 0);
    lv_obj_set_style_bg_opa(dots_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dots_, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_flex_flow(dots_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots_, 18, 0);
    for (int i = 0; i < kMaxDigits; ++i) {
        lv_obj_t* dot = ui_box(dots_);
        lv_obj_set_size(dot, 16, 16);
        lv_obj_set_style_radius(dot, 8, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }

    lv_obj_t* pad = ui_box(right);
    lv_obj_set_width(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pad, 10, 0);
    static const char* const kLabels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", LI_DELETE};
    for (int r = 0; r < 4; ++r) {
        lv_obj_t* row = ui_box(pad);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);
        for (int col = 0; col < 3; ++col) {
            const int i = r * 3 + col;
            const bool special = i == 9 || i == 11;
            lv_obj_t* key = pressable(row, special ? UI_KEY_BG : UI_SURFACE);
            lv_obj_set_height(key, LV_PCT(100));
            lv_obj_set_flex_grow(key, 1);
            const lv_font_t* font = i == 11 ? UI_ICON_M : special ? UI_FONT_TITLE : UI_FONT_VALUE;
            lv_obj_center(ui_label(key, font, special ? UI_TEXT2 : UI_TEXT, kLabels[i]));
            lv_obj_add_event_cb(key, raw_key_cb, LV_EVENT_CLICKED, this);
        }
    }
}

void AlarmDetail::refresh()
{
    if (!root_) return;
    const bool armed = is_armed(state_.state);
    const bool alert = armed || state_.state == "triggered";
    lv_obj_set_style_bg_color(status_card_, lv_color_hex(alert ? UI_WARN_BG : UI_SURFACE), 0);
    lv_obj_set_style_text_color(status_icon_, lv_color_hex(alert ? UI_WARN : UI_TEXT2), 0);
    lv_label_set_text(status_icon_, alert ? LI_SHIELD_ALERT : LI_SHIELD);

    int title = TR_ALARM_DISARMED;
    if (state_.state == "triggered") title = TR_ALARM_TRIGGERED;
    else if (state_.state == "arming") title = TR_ALARM_ARMING;
    else if (state_.state == "pending") title = TR_ALARM_PENDING;
    else if (state_.state == "disarming") title = TR_ALARM_DISARMING;
    else if (armed) title = TR_ALARM_ARMED;
    lv_label_set_text(status_title_, tr(static_cast<tr_key_t>(title)));
    const char* sub = armed ? tr(static_cast<tr_key_t>(mode_label(state_.state)))
                            : tr(state_.code_needed ? TR_ALARM_HINT_CODE : TR_ALARM_HINT_PICK);
    lv_label_set_text(status_sub_, sub);

    // the mode buttons change with armed / disarmed
    modes_ = modes_for(state_);
    lv_obj_clean(mode_column_);
    for (const Mode& m : modes_) {
        lv_obj_t* b = pressable(mode_column_, armed ? UI_ACTIVE : UI_SURFACE);
        lv_obj_set_size(b, LV_PCT(100), 80);
        lv_obj_set_style_pad_hor(b, 20, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 14, 0);
        ui_label(b, UI_ICON_BUTTON, UI_TEXT2, m.icon);
        ui_label(b, UI_FONT_NAME, UI_TEXT, tr(static_cast<tr_key_t>(m.label)));
        lv_obj_add_event_cb(b, raw_mode_cb, LV_EVENT_CLICKED, this);
    }
    refresh_dots();
}

void AlarmDetail::refresh_dots()
{
    for (int i = 0; i < kMaxDigits; ++i) {
        lv_obj_set_style_bg_color(lv_obj_get_child(dots_, i),
                                  lv_color_hex(i < static_cast<int>(code_.size()) ? UI_TEXT : UI_ACTIVE), 0);
    }
}

void AlarmDetail::on_key(int index)
{
    if (index == 9) {
        code_.clear();
    } else if (index == 11) {
        if (!code_.empty()) code_.pop_back();
    } else if (static_cast<int>(code_.size()) < kMaxDigits) {
        code_ += index == 10 ? '0' : static_cast<char>('1' + index);
    }
    refresh_dots();
}

void AlarmDetail::on_mode(int index)
{
    if (index < 0 || index >= static_cast<int>(modes_.size())) return;
    const Mode& m = modes_[index];
    const bool needed = state_.code_needed && (m.service == "alarm_disarm" || state_.code_arm_required);
    if (needed && code_.size() < 4) {
        ui_toast(tr(TR_ALARM_ENTER_CODE));
        return;
    }
    send_(m.service, service_json(state_, m.service, code_));
    code_.clear();
    refresh_dots();
}

} // namespace app::ui
