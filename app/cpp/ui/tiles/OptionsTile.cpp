#include "OptionsTile.h"

#include "../AttributeText.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../haptic/haptic.h"
#include "../../../ui_kit.h"

#include <cjson/cJSON.h>

#include <algorithm>

namespace app::ui {

using app::home_assistant::EntityState;

namespace {
void raw_button_cb(lv_event_t* e)
{
    auto* self = static_cast<OptionsTile*>(lv_event_get_user_data(e));
    self->button_pressed(lv_obj_get_index(lv_event_get_current_target_obj(e)) - 1); // [0] = title
}

void raw_flash_cb(lv_timer_t* t) { static_cast<OptionsTile*>(lv_timer_get_user_data(t))->flash_over(); }

std::string domain_of(const std::string& entity_id)
{
    const size_t dot = entity_id.find('.');
    return dot == std::string::npos ? "" : entity_id.substr(0, dot);
}

const char* icon_for(const std::string& entity_id)
{
    const std::string d = domain_of(entity_id);
    if (d == "scene") return LI_SPARKLES;
    if (d == "script") return LI_CODE;
    if (d == "light") return LI_LIGHTBULB;
    if (d == "switch" || d == "input_boolean") return LI_POWER;
    return LI_CIRCLE_DOT;
}
} // namespace

OptionsTile::OptionsTile(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService& commands)
    : instance_(std::move(instance)), commands_(&commands)
{
}

OptionsTile::OptionsTile(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService& commands,
                         std::string select_entity)
    : instance_(std::move(instance)), commands_(&commands), select_entity_(std::move(select_entity))
{
}

std::unique_ptr<OptionsTile> OptionsTile::from_entities_card(app::home_assistant::HaInstanceId instance,
                                                             app::home_assistant::HaCommandService& commands,
                                                             const std::string& raw_json)
{
    std::unique_ptr<OptionsTile> t(new OptionsTile(std::move(instance), commands));
    t->items_ = parse_entities(raw_json, &t->title_);
    if (t->items_.size() > kMaxButtons) t->items_.resize(kMaxButtons);
    return t;
}

std::vector<OptionsTile::Item> OptionsTile::parse_entities(const std::string& raw_json, std::string* title)
{
    std::vector<Item> out;
    cJSON* root = cJSON_Parse(raw_json.c_str());
    if (!root) return out;
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(root, "title");
    if (title && cJSON_IsString(t)) *title = t->valuestring;
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "entities"))
    {
        Item item;
        if (cJSON_IsString(e)) {
            item.entity_id = e->valuestring;
        } else if (cJSON_IsObject(e)) {
            const cJSON* id = cJSON_GetObjectItemCaseSensitive(e, "entity");
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(e, "name");
            if (cJSON_IsString(id)) item.entity_id = id->valuestring;
            if (cJSON_IsString(name)) item.name = name->valuestring;
        }
        if (!item.entity_id.empty()) out.push_back(std::move(item));
    }
    cJSON_Delete(root);
    return out;
}

std::vector<DataRequirement> OptionsTile::requirements() const
{
    std::vector<DataRequirement> out;
    if (select_mode()) out.push_back(EntityRequirement{instance_, select_entity_});
    for (const auto& item : items_) out.push_back(EntityRequirement{instance_, item.entity_id});
    return out;
}

void OptionsTile::create(lv_obj_t* parent)
{
    lv_obj_t* tile = ui_tile(parent);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE); // only the buttons react
    lv_obj_set_style_pad_row(tile, UI_GAP_LIST, 0);
    title_label_ = ui_label_line(tile, UI_FONT_NAME, UI_TEXT, title_.c_str());
    lv_obj_set_style_pad_bottom(title_label_, 4, 0);
    const size_t count = select_mode() ? kMaxButtons : items_.size();
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t* b = ui_box(tile);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 12, 0);
        ui_label(b, UI_ICON_BUTTON, UI_TEXT3, "");
        lv_obj_t* l = ui_label(b, UI_FONT_NAME, UI_TEXT, "");
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_add_event_cb(b, raw_button_cb, LV_EVENT_CLICKED, this);
        haptic_add_click(b, HAPTIC_BUTTON_PRESS);
        buttons_.push_back(b);
    }
    render();
    track_root(tile);
}

void OptionsTile::on_data(const DataUpdate& update)
{
    if (!update.entity_state) return;
    const EntityState& s = *update.entity_state;
    if (select_mode() && s.entity_id == select_entity_) {
        if (title_.empty()) title_ = find_attr(s.attributes, "friendly_name");
        selected_ = s.state;
        options_.clear();
        cJSON* opts = cJSON_Parse(s.attributes.count("options") ? s.attributes.at("options").c_str() : "[]");
        const cJSON* o = nullptr;
        cJSON_ArrayForEach(o, opts)
        {
            if (cJSON_IsString(o)) options_.emplace_back(o->valuestring);
        }
        cJSON_Delete(opts);
    } else {
        for (auto& item : items_) {
            if (item.entity_id == s.entity_id && item.name.empty()) item.name = find_attr(s.attributes, "friendly_name");
        }
    }
    render();
}

void OptionsTile::render()
{
    if (!title_label_) return;
    lv_label_set_text(title_label_, title_.c_str());

    // select mode: up to three options, the selected one always among them
    std::vector<std::string> shown = options_;
    if (shown.size() > kMaxButtons) {
        auto it = std::find(shown.begin(), shown.end(), selected_);
        const size_t sel = it == shown.end() ? 0 : static_cast<size_t>(it - shown.begin());
        const size_t start = sel < kMaxButtons ? 0 : sel - (kMaxButtons - 1);
        shown = std::vector<std::string>(shown.begin() + start, shown.begin() + start + kMaxButtons);
    }

    for (size_t i = 0; i < buttons_.size(); ++i) {
        lv_obj_t* b = buttons_[i];
        lv_obj_t* icon = lv_obj_get_child(b, 0);
        lv_obj_t* label = lv_obj_get_child(b, 1);
        bool active;
        if (select_mode()) {
            if (i >= shown.size()) {
                lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
            active = shown[i] == selected_;
            lv_label_set_text(label, shown[i].c_str());
            lv_label_set_text(icon, active ? LI_CHECK : "");
            if (active) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            active = static_cast<int>(i) == flashing_;
            lv_label_set_text(label, items_[i].name.empty() ? items_[i].entity_id.c_str() : items_[i].name.c_str());
            lv_label_set_text(icon, icon_for(items_[i].entity_id));
        }
        lv_obj_set_style_bg_color(b, lv_color_hex(active ? UI_ACTIVE : UI_SURFACE2), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(label, lv_color_hex(active || !select_mode() ? UI_TEXT : UI_TEXT2), 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(active ? UI_ACCENT : UI_TEXT3), 0);
    }
}

void OptionsTile::button_pressed(size_t index)
{
    if (select_mode()) {
        // the label is the option (render() picked which three are shown)
        if (index >= buttons_.size()) return;
        const std::string option = lv_label_get_text(lv_obj_get_child(buttons_[index], 1));
        if (option.empty() || option == selected_) return;
        selected_ = option; // optimistic; HA's update confirms
        render();
        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "option", option.c_str());
        char* json = cJSON_PrintUnformatted(data);
        pending_.cancel();
        pending_ = commands_->call_service(domain_of(select_entity_), "select_option", select_entity_, json,
                                           [](const app::home_assistant::ResultMessage&) {});
        cJSON_free(json);
        cJSON_Delete(data);
        return;
    }
    if (index >= items_.size()) return;
    const Item& item = items_[index];
    const std::string d = domain_of(item.entity_id);
    const bool toggles = d == "switch" || d == "light" || d == "input_boolean" || d == "fan";
    pending_.cancel();
    pending_ = commands_->call_service(d, toggles ? "toggle" : "turn_on", item.entity_id, "",
                                       [](const app::home_assistant::ResultMessage&) {});
    flashing_ = static_cast<int>(index);
    render();
    if (flash_timer_) lv_timer_delete(flash_timer_);
    flash_timer_ = lv_timer_create(raw_flash_cb, 1500, this);
    lv_timer_set_repeat_count(flash_timer_, 1);
    const std::string name = item.name.empty() ? item.entity_id : item.name;
    ui_toast((d == "scene" ? trf(TR_TILE_SCENE, {name}) : name).c_str());
}

void OptionsTile::flash_over()
{
    flash_timer_ = nullptr; // one-shot: deleted by LVGL after this call
    flashing_ = -1;
    render();
}

void OptionsTile::shutdown_ui()
{
    pending_.cancel();
    if (flash_timer_) lv_timer_delete(flash_timer_);
    flash_timer_ = nullptr;
}

} // namespace app::ui
