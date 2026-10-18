#include "ui_kit.h"

#include <stdlib.h>
#include <string.h>

uint32_t ui_look_bg(ui_look_t look)
{
    switch (look) {
    case UI_LOOK_ON: return UI_ACCENT_BG;
    case UI_LOOK_WARN: return UI_WARN_BG;
    case UI_LOOK_HEAT: return UI_HEAT_BG;
    case UI_LOOK_COOL: return UI_COOL_BG;
    default: return UI_SURFACE2;
    }
}

uint32_t ui_look_fg(ui_look_t look)
{
    switch (look) {
    case UI_LOOK_ON: return UI_ACCENT;
    case UI_LOOK_INFO: return UI_TEXT;
    case UI_LOOK_WARN: return UI_WARN;
    case UI_LOOK_HEAT: return UI_HEAT;
    case UI_LOOK_COOL: return UI_COOL;
    default: return UI_TEXT3;
    }
}

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}

lv_obj_t *ui_tile(lv_obj_t *parent)
{
    lv_obj_t *t = ui_box(parent);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(t, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x1A1A18), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, UI_R_TILE, 0);
    lv_obj_set_style_pad_all(t, UI_TILE_PAD, 0);
    lv_obj_set_style_pad_row(t, UI_GAP, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT), 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    return t;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text ? text : "");
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

lv_obj_t *ui_label_line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, color, text);
    lv_obj_set_width(l, LV_PCT(100));
    /* one line high: with a content height the label would wrap instead of cutting */
    lv_obj_set_height(l, lv_font_get_line_height(font));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

lv_obj_t *ui_bubble(lv_obj_t *parent, int32_t size, const lv_font_t *icon_font, const char *icon, ui_look_t look)
{
    lv_obj_t *b = ui_box(parent);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *i = ui_label(b, icon_font, UI_TEXT3, icon);
    lv_obj_center(i);
    ui_bubble_set(b, icon, look, 0);
    return b;
}

void ui_bubble_set(lv_obj_t *bubble, const char *icon, ui_look_t look, uint32_t icon_color)
{
    lv_obj_set_style_bg_color(bubble, lv_color_hex(ui_look_bg(look)), 0);
    lv_obj_t *i = lv_obj_get_child(bubble, 0);
    if (!i) return;
    if (icon && strcmp(lv_label_get_text(i), icon) != 0) lv_label_set_text(i, icon);
    lv_obj_set_style_text_color(i, lv_color_hex(icon_color ? icon_color : ui_look_fg(look)), 0);
}

lv_obj_t *ui_button_row(lv_obj_t *tile)
{
    lv_obj_t *row = ui_box(tile);
    lv_obj_set_size(row, LV_PCT(100), UI_BUTTON_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_GAP_LIST, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    return row;
}

lv_obj_t *ui_tile_button(lv_obj_t *row, const char *icon, const char *label, bool primary)
{
    lv_obj_t *b = ui_box(row);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? UI_TEXT : UI_SURFACE2), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? 0xC9C6C0 : UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(primary ? 0x000000 : UI_TEXT), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 8, 0);
    lv_obj_t *i = lv_label_create(b);
    lv_obj_set_style_text_font(i, UI_ICON_BUTTON, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, UI_FONT_NAME, 0);
    ui_tile_button_set(b, icon, label, false);
    return b;
}

void ui_tile_button_set(lv_obj_t *button, const char *icon, const char *label, bool active)
{
    lv_obj_t *i = lv_obj_get_child(button, 0);
    lv_obj_t *l = lv_obj_get_child(button, 1);
    if (icon) lv_label_set_text(i, icon);
    if (label) lv_label_set_text(l, label);
    if (icon) lv_obj_remove_flag(i, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(i, LV_OBJ_FLAG_HIDDEN);
    if (label) lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    if (active) lv_obj_add_state(button, LV_STATE_CHECKED);
    else lv_obj_remove_state(button, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_ACTIVE), LV_STATE_CHECKED);
}

lv_obj_t *ui_toggle(lv_obj_t *parent)
{
    lv_obj_t *s = lv_switch_create(parent);
    lv_obj_set_size(s, 96, 56);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACTIVE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_TEXT3), LV_PART_KNOB);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_pad_all(s, -6, LV_PART_KNOB); /* knob 44 in a 56 track */
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, 0, 0);
    return s;
}

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_toast_timer = NULL; /* auto-deleted after this call */
    if (s_toast) lv_obj_delete(s_toast);
}

static void toast_deleted_cb(lv_event_t *e)
{
    if (s_toast == lv_event_get_target(e)) s_toast = NULL;
}

void ui_toast(const char *text)
{
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) lv_obj_delete(s_toast);
    lv_obj_t *t = ui_box(lv_layer_top());
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(t, 24, 0);
    lv_obj_set_style_pad_ver(t, 14, 0);
    ui_label(t, UI_FONT_BODY, UI_TEXT, text);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_add_event_cb(t, toast_deleted_cb, LV_EVENT_DELETE, NULL);
    s_toast = t;
    s_toast_timer = lv_timer_create(toast_timer_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* ── Gestures ── */

#define DOUBLE_TAP_MS 300

typedef struct {
    ui_gesture_cb_t tap, hold, double_tap;
    void *user;
    lv_timer_t *pending_tap; /* waiting whether a second tap follows */
} gestures_t;

static void pending_tap_cb(lv_timer_t *t)
{
    gestures_t *g = lv_timer_get_user_data(t);
    g->pending_tap = NULL; /* repeat count 1: deleted after this call */
    if (g->tap) g->tap(g->user);
}

static void gestures_event_cb(lv_event_t *e)
{
    gestures_t *g = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:
        if (!g->double_tap) {
            if (g->tap) g->tap(g->user);
        } else if (g->pending_tap) {
            lv_timer_delete(g->pending_tap);
            g->pending_tap = NULL;
            g->double_tap(g->user);
        } else {
            g->pending_tap = lv_timer_create(pending_tap_cb, DOUBLE_TAP_MS, g);
            lv_timer_set_repeat_count(g->pending_tap, 1);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (g->hold) g->hold(g->user);
        break;
    case LV_EVENT_DELETE:
        if (g->pending_tap) lv_timer_delete(g->pending_tap);
        free(g);
        break;
    default: break;
    }
}

void ui_gestures(lv_obj_t *obj, ui_gesture_cb_t tap, ui_gesture_cb_t hold, ui_gesture_cb_t double_tap, void *user)
{
    gestures_t *g = calloc(1, sizeof(*g));
    g->tap = tap;
    g->hold = hold;
    g->double_tap = double_tap;
    g->user = user;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_SHORT_CLICKED, g);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_LONG_PRESSED, g);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_DELETE, g);
}

lv_obj_t *ui_toggle_large(lv_obj_t *parent)
{
    lv_obj_t *s = ui_toggle(parent);
    lv_obj_set_size(s, 112, 64);
    return s;
}

/* ── Detail views ── */

#define UI_DETAIL_FLAG LV_OBJ_FLAG_USER_2 /* marks a detail page's root */

void ui_detail_close_all(void)
{
    lv_obj_t *screen = lv_screen_active();
    for (int32_t i = (int32_t)lv_obj_get_child_count(screen) - 1; i >= 0; --i) {
        lv_obj_t *o = lv_obj_get_child(screen, i);
        if (lv_obj_has_flag(o, UI_DETAIL_FLAG)) lv_obj_delete(o);
    }
}

static void detail_back_cb(lv_event_t *e)
{
    lv_obj_delete_async(lv_event_get_user_data(e));
}

ui_detail_t ui_detail_open(const char *title, const char *subtitle)
{
    ui_detail_t d;
    d.root = ui_box(lv_screen_active());
    lv_obj_add_flag(d.root, UI_DETAIL_FLAG);
    lv_obj_remove_flag(d.root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(d.root, LV_OBJ_FLAG_CLICKABLE); /* nothing below reacts */
    lv_obj_set_size(d.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(d.root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(d.root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(d.root, UI_EDGE, 0);
    lv_obj_set_style_pad_row(d.root, UI_EDGE, 0);
    lv_obj_set_flex_flow(d.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(d.root, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *head = ui_box(d.root);
    lv_obj_set_size(head, LV_PCT(100), 72);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 16, 0);
    lv_obj_t *back = ui_box(head);
    lv_obj_remove_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(back, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(back, UI_ICON_M, UI_TEXT, LI_CHEVRON_LEFT));
    lv_obj_add_event_cb(back, detail_back_cb, LV_EVENT_CLICKED, d.root);

    lv_obj_t *texts = ui_box(head);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, 0);
    d.title = ui_label_line(texts, UI_FONT_TITLE, UI_TEXT, title);
    d.subtitle = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, subtitle ? subtitle : "");
    if (!subtitle) lv_obj_add_flag(d.subtitle, LV_OBJ_FLAG_HIDDEN);

    d.right = ui_box(head);
    lv_obj_set_size(d.right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(d.right, LV_OBJ_FLAG_EVENT_BUBBLE);

    d.content = ui_box(d.root);
    lv_obj_remove_flag(d.content, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_width(d.content, LV_PCT(100));
    lv_obj_set_flex_grow(d.content, 1);
    lv_obj_set_flex_flow(d.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d.content, UI_EDGE, 0);
    return d;
}

/* ── Segmented control ── */

static void segment_cb(lv_event_t *e)
{
    lv_obj_t *seg = lv_obj_get_parent(lv_event_get_current_target_obj(e));
    const int index = (int)lv_obj_get_index(lv_event_get_current_target_obj(e));
    if (index == ui_segmented_selected(seg)) return;
    ui_segmented_select(seg, index);
    lv_obj_send_event(seg, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_segmented(lv_obj_t *parent, const char *const *labels, int count)
{
    lv_obj_t *seg = ui_box(parent);
    lv_obj_remove_flag(seg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(seg, LV_PCT(100), 64);
    lv_obj_set_style_radius(seg, 22, 0);
    lv_obj_set_style_bg_color(seg, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(seg, 6, 0);
    lv_obj_set_style_pad_column(seg, 6, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < count; ++i) {
        lv_obj_t *b = ui_box(seg);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT2), 0);
        lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT), LV_STATE_CHECKED);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, UI_FONT_NAME, 0);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, segment_cb, LV_EVENT_CLICKED, NULL);
    }
    ui_segmented_select(seg, 0);
    return seg;
}

void ui_segmented_select(lv_obj_t *seg, int index)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(seg); ++i) {
        lv_obj_t *b = lv_obj_get_child(seg, (int32_t)i);
        if ((int)i == index) lv_obj_add_state(b, LV_STATE_CHECKED);
        else lv_obj_remove_state(b, LV_STATE_CHECKED);
    }
}

int ui_segmented_selected(lv_obj_t *seg)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(seg); ++i) {
        if (lv_obj_has_state(lv_obj_get_child(seg, (int32_t)i), LV_STATE_CHECKED)) return (int)i;
    }
    return -1;
}

/* ── Slider row ── */

typedef struct {
    int value;
} slider_row_t;

static void slider_row_event_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    slider_row_t *st = lv_obj_get_user_data(row);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        free(st);
        return;
    }
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_PRESSED) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(row, &a);
    const int32_t w = lv_area_get_width(&a);
    int v = w > 0 ? (int)((p.x - a.x1) * 100 / w) : 0;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    if (v == st->value) return;
    st->value = v;
    lv_obj_set_width(lv_obj_get_child(row, 0), LV_PCT(v));
    lv_obj_send_event(row, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_slider_row(lv_obj_t *parent, const char *icon, const char *label)
{
    lv_obj_t *row = ui_box(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(row, LV_PCT(100), 72);
    lv_obj_set_style_radius(row, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(row, true, 0);
    slider_row_t *st = calloc(1, sizeof(*st));
    lv_obj_set_user_data(row, st);

    lv_obj_t *fill = ui_box(row); /* [0] */
    lv_obj_set_size(fill, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(fill, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);

    lv_obj_t *left = ui_box(row); /* [1] */
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 12, 0);
    if (icon) ui_label(left, UI_ICON_BUTTON, UI_TEXT2, icon);
    ui_label(left, UI_FONT_BODY, UI_TEXT, label);

    lv_obj_t *value = ui_label(row, UI_FONT_NAME, UI_TEXT, ""); /* [2] */
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -24, 0);

    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_DELETE, NULL);
    return row;
}

void ui_slider_row_set(lv_obj_t *row, int value, const char *value_text)
{
    slider_row_t *st = lv_obj_get_user_data(row);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    st->value = value;
    lv_obj_set_width(lv_obj_get_child(row, 0), LV_PCT(value));
    lv_label_set_text(lv_obj_get_child(row, 2), value_text ? value_text : "");
}

int ui_slider_row_value(lv_obj_t *row)
{
    slider_row_t *st = lv_obj_get_user_data(row);
    return st ? st->value : 0;
}
