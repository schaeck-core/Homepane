/* Building blocks of the design guide (docs/design/DESIGN_GUIDE.md):
 * dashboard tiles, icon circles, tile buttons, toggles, toasts and the
 * tap / double tap / hold gestures. Colours and sizes from ui_theme.h. */
#ifndef UI_KIT_H
#define UI_KIT_H

#include "lvgl/lvgl.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Colour pair of an icon circle: background and icon. */
typedef enum {
    UI_LOOK_OFF,  /* off / info-less: surface 2, text 3 */
    UI_LOOK_ON,   /* on: sand on dark sand */
    UI_LOOK_INFO, /* neutral state: surface 2, text */
    UI_LOOK_WARN, /* attention: warning on dark red */
    UI_LOOK_HEAT,
    UI_LOOK_COOL,
} ui_look_t;

uint32_t ui_look_bg(ui_look_t look);
uint32_t ui_look_fg(ui_look_t look);

/* A plain container without theme styles (transparent, no padding, not
 * scrollable, not clickable): the base of every layout box. */
lv_obj_t *ui_box(lv_obj_t *parent);

/* Dashboard tile: surface, radius 24, padding 16, column layout, fills the
 * parent cell. Pressing darkens it slightly. */
lv_obj_t *ui_tile(lv_obj_t *parent);

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
/* Single-line label cut with "..." at the parent's width. */
lv_obj_t *ui_label_line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);

/* Round icon circle (diameter `size`) with a Lucide icon. */
lv_obj_t *ui_bubble(lv_obj_t *parent, int32_t size, const lv_font_t *icon_font, const char *icon, ui_look_t look);
/* icon_color 0: the look's own colour (e.g. a light's colour instead of sand). */
void ui_bubble_set(lv_obj_t *bubble, const char *icon, ui_look_t look, uint32_t icon_color);

/* Row of tile buttons at the bottom of a tile (64 high, gap 8); add
 * buttons with ui_tile_button(), they share the width equally. */
lv_obj_t *ui_button_row(lv_obj_t *tile);
/* icon and/or label (NULL to leave out); primary: light button, black content. */
lv_obj_t *ui_tile_button(lv_obj_t *row, const char *icon, const char *label, bool primary);
void ui_tile_button_set(lv_obj_t *button, const char *icon, const char *label, bool active);

/* Toggle 96 x 56: sand track and black knob when on, "active" track and
 * text 3 knob when off. */
lv_obj_t *ui_toggle(lv_obj_t *parent);
/* Large toggle for detail headers (112 x 64, knob 52). */
lv_obj_t *ui_toggle_large(lv_obj_t *parent);

/* ── Detail views ──
 * Full-screen page over the dashboard: header (72) with the back button
 * always top left, title 28 with an optional subtitle, an optional control
 * on the right; content below (column, gap 16). Deleting `root` closes it;
 * the back button does that. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *right;   /* header slot right of the title (empty box) */
    lv_obj_t *content; /* column, fills the rest */
} ui_detail_t;
ui_detail_t ui_detail_open(const char *title, const char *subtitle);
/* Closes every open detail page (the screensaver's reset). */
void ui_detail_close_all(void);

/* Segmented control (64 high, radius 18, black ground, padding 6): the
 * segments are children of the returned object. Tapping one selects it
 * and sends LV_EVENT_VALUE_CHANGED on the control; ui_segmented_selected()
 * reads the index. */
lv_obj_t *ui_segmented(lv_obj_t *parent, const char *const *labels, int count);
void ui_segmented_select(lv_obj_t *seg, int index);
int ui_segmented_selected(lv_obj_t *seg);

/* Slider as a row (design guide): the filled share is the "active"
 * surface, icon + label left, value right; the whole row is the control.
 * LV_EVENT_VALUE_CHANGED while dragging and LV_EVENT_RELEASED at the end;
 * the value 0..100 via ui_slider_row_value(). */
lv_obj_t *ui_slider_row(lv_obj_t *parent, const char *icon, const char *label);
void ui_slider_row_set(lv_obj_t *row, int value, const char *value_text);
int ui_slider_row_value(lv_obj_t *row);

/* Short hint at the bottom of the screen (pill, ~2 s). */
void ui_toast(const char *text);

/* Gestures of dashboard tiles: tap, hold (480 ms) and an optional double
 * tap. Without a double tap handler a tap fires at once; with one, a single
 * tap waits for the double tap window. Handlers get `user`. */
typedef void (*ui_gesture_cb_t)(void *user);
void ui_gestures(lv_obj_t *obj, ui_gesture_cb_t tap, ui_gesture_cb_t hold, ui_gesture_cb_t double_tap, void *user);

#ifdef __cplusplus
}
#endif

#endif /* UI_KIT_H */
