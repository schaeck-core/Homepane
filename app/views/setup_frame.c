#include "setup_frame.h"

#include "../ui_kit.h"

static lv_obj_t *step_bar(lv_obj_t *parent, int step)
{
    lv_obj_t *bar = ui_box(parent);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 6, 0);
    for (int i = 1; i <= SETUP_STEP_COUNT; i++) {
        lv_obj_t *seg = ui_box(bar);
        lv_obj_set_size(seg, i == step ? 40 : 20, 6);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(i <= step ? UI_ACCENT : UI_INACTIVE), 0);
    }
    return bar;
}

setup_frame_t setup_frame_create(lv_obj_t *parent, int step, const char *title, const char *subtitle,
                                 lv_event_cb_t on_back)
{
    setup_frame_t f;
    lv_obj_clean(parent);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_BG), 0);

    f.root = ui_box(parent);
    lv_obj_set_size(f.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(f.root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(f.root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(f.root, UI_EDGE, 0);
    lv_obj_set_style_pad_top(f.root, 20, 0);
    lv_obj_set_style_pad_row(f.root, 20, 0);
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(f.root, lv_color_hex(UI_TEXT), 0);

    /* header: back button left (guide: always top left), texts */
    lv_obj_t *head = ui_box(f.root);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(head, 16, 0);
    if (on_back) {
        lv_obj_t *back = ui_box(head);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(back, UI_TOUCH_MIN, UI_TOUCH_MIN);
        lv_obj_set_style_radius(back, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_center(ui_label(back, UI_ICON_M, UI_TEXT, LI_CHEVRON_LEFT));
        lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *texts = ui_box(head);
    lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 6, 0);
    step_bar(texts, step);
    f.title = ui_label(texts, UI_FONT_TITLE, UI_TEXT, title);
    lv_obj_set_style_pad_top(f.title, 6, 0);
    f.subtitle = ui_label(texts, UI_FONT_LABEL, UI_TEXT2, subtitle ? subtitle : "");
    lv_obj_set_width(f.subtitle, LV_PCT(100));
    lv_label_set_long_mode(f.subtitle, LV_LABEL_LONG_WRAP);
    if (!subtitle) lv_obj_add_flag(f.subtitle, LV_OBJ_FLAG_HIDDEN);

    f.content = ui_box(f.root);
    lv_obj_set_width(f.content, LV_PCT(100));
    lv_obj_set_flex_grow(f.content, 1);
    lv_obj_set_flex_flow(f.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.content, UI_GAP, 0);

    f.footer = ui_box(f.root);
    lv_obj_set_size(f.footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(f.footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(f.footer, UI_GAP, 0);
    return f;
}

lv_obj_t *setup_button(lv_obj_t *footer, const char *icon, const char *label, bool primary, lv_event_cb_t on_click,
                       void *user)
{
    const uint32_t fg = primary ? UI_BG : UI_TEXT;
    lv_obj_t *btn = ui_box(footer);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(btn, 88);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, UI_R_KEY, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? UI_ACCENT : UI_SURFACE), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? UI_AUTO : UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 10, 0);
    if (icon) ui_label(btn, UI_ICON_BUTTON, fg, icon);
    ui_label(btn, UI_FONT_NAME, fg, label);
    if (on_click) lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, user);
    return btn;
}

void setup_button_set_enabled(lv_obj_t *button, bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

lv_obj_t *setup_choice_row(lv_obj_t *parent, const char *icon, const char *title, const char *subtitle,
                           bool selected, lv_event_cb_t on_click, void *user)
{
    lv_obj_t *row = ui_box(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_PCT(100), 88);
    lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_CHECKED);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);

    if (icon) ui_bubble(row, 56, UI_ICON_BUTTON, icon, UI_LOOK_INFO);
    lv_obj_t *texts = ui_box(row);
    lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, 0);
    ui_label_line(texts, UI_FONT_NAME, UI_TEXT, title);
    if (subtitle && subtitle[0]) ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, subtitle);
    /* last child: the check mark */
    ui_label(row, UI_ICON_M, UI_ACCENT, LI_CHECK);
    setup_choice_row_set_selected(row, selected);
    if (on_click) lv_obj_add_event_cb(row, on_click, LV_EVENT_CLICKED, user);
    return row;
}

void setup_choice_row_set_selected(lv_obj_t *row, bool selected)
{
    lv_obj_t *check = lv_obj_get_child(row, -1);
    if (selected) {
        lv_obj_add_state(row, LV_STATE_CHECKED);
        lv_obj_remove_flag(check, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_state(row, LV_STATE_CHECKED);
        lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *setup_message(lv_obj_t *parent, const char *text, bool error)
{
    lv_obj_t *l = ui_label(parent, UI_FONT_LABEL, error ? UI_WARN : UI_TEXT2, text);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}
