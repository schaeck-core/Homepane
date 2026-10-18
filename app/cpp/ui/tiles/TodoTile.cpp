#include "TodoTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cjson/cJSON.h>

#include <cstdint>

namespace app::ui {

namespace {
constexpr size_t kShown = 4;
constexpr uint32_t kRefreshMs = 5 * 60 * 1000;

void raw_item_cb(lv_event_t* e)
{
    auto* self = static_cast<TodoTile*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_current_target_obj(e);
    self->item_tapped(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row))));
}
} // namespace

TodoTile::~TodoTile()
{
    pending_.cancel();
    completing_.cancel();
}

std::vector<TodoTile::Item> TodoTile::parse_open_items(const std::string& result_json)
{
    std::vector<Item> out;
    cJSON* root = cJSON_Parse(result_json.c_str());
    if (!root) return out;
    const cJSON* it = nullptr;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items"))
    {
        const cJSON* uid = cJSON_GetObjectItemCaseSensitive(it, "uid");
        const cJSON* summary = cJSON_GetObjectItemCaseSensitive(it, "summary");
        const cJSON* status = cJSON_GetObjectItemCaseSensitive(it, "status");
        if (!cJSON_IsString(uid) || !cJSON_IsString(summary)) continue;
        if (cJSON_IsString(status) && std::string(status->valuestring) == "completed") continue;
        out.push_back(Item{uid->valuestring, summary->valuestring, false});
    }
    cJSON_Delete(root);
    return out;
}

void TodoTile::build(lv_obj_t* tile)
{
    lv_obj_t* top = ui_box(tile);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    name_label_ = ui_label_line(top, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_width(name_label_, 0);
    lv_obj_set_flex_grow(name_label_, 1);
    count_label_ = ui_label(top, UI_FONT_NAME, UI_ACCENT, "");

    list_ = ui_box(tile);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_, 4, 0);

    refresh_ = lv_timer_create([](lv_timer_t* t) { static_cast<TodoTile*>(lv_timer_get_user_data(t))->fetch(); },
                               kRefreshMs, this);
}

void TodoTile::update()
{
    if (!list_) return;
    size_t open = 0;
    for (const Item& it : items_) open += it.done ? 0 : 1;
    lv_label_set_text(name_label_, name().c_str());
    lv_label_set_text(count_label_, fetched_ && open > 0 ? std::to_string(open).c_str() : "");
    lv_obj_clean(list_);
    if (!fetched_) {
        ui_label(list_, UI_FONT_LABEL, UI_TEXT2, "…");
        return;
    }
    if (open == 0) {
        ui_label(list_, UI_FONT_LABEL, UI_TEXT2, tr(TR_TODO_ALL_DONE));
        return;
    }
    size_t shown = 0;
    for (size_t i = 0; i < items_.size() && shown < kShown; ++i) {
        if (items_[i].done) continue;
        ++shown;
        // the whole row checks the item off
        lv_obj_t* row = ui_box(list_);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), 48);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_t* ring = ui_box(row);
        lv_obj_set_size(ring, 26, 26);
        lv_obj_set_style_radius(ring, 13, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(UI_TEXT3), 0);
        lv_obj_t* text = ui_label_line(row, UI_FONT_BODY, UI_TEXT, items_[i].summary.c_str());
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(row, raw_item_cb, LV_EVENT_CLICKED, this);
    }
    if (open > kShown) {
        ui_label(list_, UI_FONT_LABEL, UI_TEXT2, trf(TR_TODO_MORE, {std::to_string(open - kShown)}).c_str());
    }
}

void TodoTile::item_tapped(int index)
{
    if (index < 0 || index >= static_cast<int>(items_.size()) || items_[index].done || !commands()) return;
    // gone at once (hidden: this runs inside the row's own click event, so
    // the list is not rebuilt here); the next fetch() rebuilds it
    items_[index].done = true;
    const Item item = items_[index];
    for (uint32_t i = 0; i < lv_obj_get_child_count(list_); ++i) {
        lv_obj_t* row = lv_obj_get_child(list_, static_cast<int32_t>(i));
        if (lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE) &&
            reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)) == index) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    size_t open = 0;
    for (const Item& it : items_) open += it.done ? 0 : 1;
    lv_label_set_text(count_label_, open > 0 ? std::to_string(open).c_str() : "");
    ui_toast(trf(TR_TODO_DONE_TOAST, {item.summary}).c_str());

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "item", item.uid.c_str());
    cJSON_AddStringToObject(data, "status", "completed");
    char* json = cJSON_PrintUnformatted(data);
    const std::string extra = json ? json : "";
    if (json) cJSON_free(json);
    cJSON_Delete(data);
    completing_.cancel();
    completing_ = commands()->call_service("todo", "update_item", entity_id(), extra,
                                           [this](const app::home_assistant::ResultMessage& r) {
                                               if (!r.success) ui_toast(trf(TR_DASHBOARD_COMMAND_FAILED, {name()}).c_str());
                                               fetch(); // the real list either way
                                           });
}

void TodoTile::fetch()
{
    if (!commands()) return;
    pending_.cancel();
    pending_ = commands()->list_todo_items(entity_id(), [this](const app::home_assistant::ResultMessage& r) {
        if (!r.success) return;
        items_ = parse_open_items(r.result_json);
        fetched_ = true;
        update();
    });
}

void TodoTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    if (u.entity_state) fetch(); // the state is the number of open items
}

void TodoTile::shutdown_ui()
{
    pending_.cancel();
    completing_.cancel();
    if (refresh_) {
        lv_timer_delete(refresh_);
        refresh_ = nullptr;
    }
    list_ = nullptr;
    TileWidget::shutdown_ui();
}

} // namespace app::ui
