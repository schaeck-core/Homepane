/**
 * @file settings.c
 * @brief Settings view: full-screen, touch-first layout for the 4" 720x720
 *        panel.
 *
 * Home: a grid of large category tiles. Tapping a tile opens that tab's
 * page full width; the header shows a back arrow (left) and a large close
 * button (right) on every screen. Tabs register themselves via
 * settings_register_tab() and build their page with settings_page_create()/
 * settings_row_create(); controls inside are enlarged for touch
 * automatically (apply_touch_style()), so tabs keep their simple code.
 *
 * Replaces the earlier lv_menu-based dialog (95% x 90%, small sidebar
 * entries, tiny root back button in the corner).
 */

#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "settings.h"
#include "../views.h"
#include "settings_tab.h"
#include "general.h"
#include "clock.h"
#include "screensaver.h"
#include "homeassistant.h"
#include "wifi.h"
#include "update.h"
#include "../../config.h"
#include "../../fonts.h"
#include "../../addons/addons.h"
#include "../keyboard_style.h"
#include "../../ui_kit.h"

// ── Look: docs/design/DESIGN_GUIDE.md (Settings artboards) ──────────────────
#define COLOR_BG          UI_BG
#define COLOR_CARD        UI_SURFACE
#define COLOR_CARD_PRESS  UI_ACTIVE

#define HEADER_HEIGHT     72
#define HEADER_BTN_SIZE   UI_TOUCH_MIN
#define TILE_HEIGHT       192 /* three rows fill the screen */
#define ROW_MIN_HEIGHT    88

// ── State ────────────────────────────────────────────────────────────────────
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_back_btn = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_body = NULL;
static lv_obj_t *s_page = NULL;          // currently shown tab page (NULL on home)
static lv_obj_t *settings_keyboard = NULL;
static bool is_initialized = false;

static void show_home(void);
static void show_tab(int index);

void settings_init(void)
{
    if (is_initialized) return;

    // Clear any previously registered tabs
    settings_clear_tabs();

    // Register built-in tabs
    settings_general_init();
    settings_clock_init();
    settings_screensaver_init();
    settings_wifi_init();
    settings_homeassistant_init();
    settings_update_init();

    // Addons (started with the dashboard, or now) and their settings tabs
    addons_init();
    addons_register_settings();

    is_initialized = true;
    printf("[Settings] Initialized with tabs\n");
}

// ── Shared on-screen keyboard ────────────────────────────────────────────────
static void keyboard_hide(void)
{
    if (!settings_keyboard) return;
    lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(settings_keyboard, NULL);
    if (s_page) lv_obj_set_style_pad_bottom(s_page, 16, 0);
}

// Shows/hides the shared settings keyboard as textareas gain/lose focus.
// On real hardware (touch-only, no physical keyboard indev) this is the
// only way to type into a settings textarea at all.
static void keyboard_ta_event_cb(lv_event_t *e)
{
    if (!settings_keyboard) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(settings_keyboard, ta);
        lv_obj_remove_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_keyboard);
        // Keep the field visible above the keyboard (it covers the lower half).
        if (s_page) {
            lv_obj_set_style_pad_bottom(s_page, lv_obj_get_height(settings_keyboard) + 16, 0);
            lv_obj_update_layout(s_page);
        }
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (code == LV_EVENT_DEFOCUSED) {
        keyboard_hide();
    }
}

// Keyboard's checkmark/close keys: finish editing. Sending DEFOCUSED makes
// the tab's own save-on-defocus handler run (tabs persist values there).
static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(settings_keyboard);
    keyboard_hide();
    if (ta) {
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        lv_obj_send_event(ta, LV_EVENT_DEFOCUSED, NULL);
    }
}

void settings_attach_keyboard(lv_obj_t *textarea)
{
    lv_obj_add_event_cb(textarea, keyboard_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(textarea, keyboard_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);
}

// ── Page building helpers for tabs ───────────────────────────────────────────
lv_obj_t *settings_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(page, UI_EDGE, 0);
    lv_obj_set_style_pad_bottom(page, UI_EDGE, 0);
    lv_obj_set_style_pad_row(page, UI_GAP_LIST, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *settings_row_create(lv_obj_t *page)
{
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, ROW_MIN_HEIGHT, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
    lv_obj_set_style_pad_hor(row, 20, 0);
    lv_obj_set_style_pad_ver(row, 12, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    // ROW_WRAP (not ROW): LVGL only applies the track placement below when
    // wrapping is enabled — without it, rows with short content (e.g. a
    // single status label) stick to the top of their minimum height.
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_USER_1); // marks a settings row for apply_touch_style()
    // Clickable rows (e.g. "System Information") get press feedback.
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD_PRESS), LV_STATE_PRESSED);
    return row;
}

static bool is_icon_font(const lv_font_t *f)
{
    return f && (f == font_lucide_18 || f == font_lucide_24 || f == font_lucide_28 || f == font_lucide_36 ||
                 f == font_lucide_48 || f == font_lucide_56);
}

// A row holding nothing but one plain text label is an explanatory hint:
// render it as dim text under the cards instead of as a card of its own.
static void style_hint_row(lv_obj_t *row)
{
    if (lv_obj_get_child_count(row) != 1) return;
    lv_obj_t *only = lv_obj_get_child(row, 0);
    if (!lv_obj_check_type(only, &lv_label_class)) return;
    if (is_icon_font(lv_obj_get_style_text_font(only, 0))) return;
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_min_height(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_text_font(only, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(only, lv_color_hex(UI_TEXT2), 0);
    lv_label_set_long_mode(only, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(only, LV_PCT(100));
}

// Enlarges and restyles stock widgets inside a tab page for touch use, so
// every tab gets consistent, finger-sized controls without each having to
// style them itself.
static void apply_touch_style(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) style_hint_row(obj);

    if (lv_obj_check_type(obj, &lv_switch_class)) {
        // Toggle 96 x 56: sand track and black knob when on (design guide)
        lv_obj_set_size(obj, 96, 56);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACTIVE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACTIVE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_TEXT3), LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_KNOB | LV_STATE_CHECKED);
        lv_obj_set_style_pad_all(obj, -6, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(obj, 0, LV_PART_KNOB);
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(obj, 12);
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        // A row's value + chevron (design guide list row); the list opens below.
        // the width the tab asked for (the layout has not run yet: get_width() would be 0)
        const int32_t w = lv_obj_get_style_width(obj, LV_PART_MAIN);
        if (LV_COORD_IS_SPEC(w) || w < 190) lv_obj_set_width(obj, 190);
        lv_obj_set_height(obj, 56);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 14, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(UI_TEXT2), 0);
        lv_obj_set_style_text_font(obj, UI_FONT_BODY, 0);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_pad_ver(obj, 14, 0);
        lv_obj_set_style_pad_left(obj, 12, 0);
        lv_obj_set_style_pad_right(obj, 8, 0);
        lv_dropdown_set_symbol(obj, LI_CHEVRON_DOWN);
        lv_obj_set_style_text_font(obj, UI_ICON_BUTTON, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(obj, lv_color_hex(UI_TEXT3), LV_PART_INDICATOR);
        lv_obj_t *list = lv_dropdown_get_list(obj);
        if (list) {
            lv_obj_set_style_text_font(list, UI_FONT_BODY, 0);
            lv_obj_set_style_text_line_space(list, 30, 0); // taller, tappable options
            lv_obj_set_style_bg_color(list, lv_color_hex(UI_SURFACE2), 0);
            lv_obj_set_style_border_width(list, 0, 0);
            lv_obj_set_style_text_color(list, lv_color_hex(UI_TEXT), 0);
            lv_obj_set_style_radius(list, UI_R_CONTROL, 0);
            lv_obj_set_style_pad_ver(list, 14, 0);
            lv_obj_set_style_pad_hor(list, 16, 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(UI_ACTIVE), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_text_color(list, lv_color_hex(UI_TEXT), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_max_height(list, 380, 0);
        }
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        if (lv_obj_get_width(obj) < 260) lv_obj_set_width(obj, 260);
        lv_obj_set_height(obj, 56);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(UI_ACCENT), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_radius(obj, 14, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(UI_TEXT), 0);
        lv_obj_set_style_text_font(obj, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(UI_TEXT3), LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_pad_ver(obj, 14, 0);
        lv_obj_set_style_pad_hor(obj, 12, 0);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        return; // don't descend into the textarea's internal label/cursor
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_TEXT), LV_PART_KNOB);
        lv_obj_set_style_shadow_width(obj, 0, LV_PART_KNOB);
    } else if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, UI_R_CONTROL, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(UI_TEXT), 0);
        if (lv_obj_get_height(obj) < 56) lv_obj_set_height(obj, 56);
    }

    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) apply_touch_style(lv_obj_get_child(obj, i));
}

// Vertically centers the content of rows shorter than ROW_MIN_HEIGHT. LVGL
// applies min-height after the flex layout, so a short row (a single status
// label, a plain button row) would otherwise keep its content at the top.
static void center_short_rows(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1) && lv_obj_get_style_bg_opa(obj, 0) != LV_OPA_TRANSP) {
        int32_t content_h = 0;
        uint32_t n = lv_obj_get_child_count(obj);
        for (uint32_t i = 0; i < n; i++) {
            int32_t h = lv_obj_get_height(lv_obj_get_child(obj, i));
            if (h > content_h) content_h = h;
        }
        int32_t pad = (ROW_MIN_HEIGHT - content_h) / 2;
        if (content_h > 0 && pad > 12) {
            lv_obj_set_style_min_height(obj, 0, 0);
            lv_obj_set_style_pad_ver(obj, pad, 0);
        }
        return;
    }
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) center_short_rows(lv_obj_get_child(obj, i));
}

// ── Header ───────────────────────────────────────────────────────────────────
static lv_obj_t *header_button(lv_obj_t *parent, const char *icon, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, HEADER_BTN_SIZE, HEADER_BTN_SIZE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_R_CONTROL, 0);
    lv_obj_set_ext_click_area(btn, 8);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_font(lbl, UI_ICON_M, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT), 0);
    lv_obj_center(lbl);
    return btn;
}

static void close_event_cb(lv_event_t *e)
{
    (void)e;
    settings_close();
}

static void back_event_cb(lv_event_t *e)
{
    (void)e;
    show_home();
}

// ── Screens ──────────────────────────────────────────────────────────────────
static lv_timer_t *s_status_timer;

static void clear_body(void)
{
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    keyboard_hide();
    s_page = NULL;
    lv_obj_clean(s_body);
    lv_obj_scroll_to_y(s_body, 0, LV_ANIM_OFF);
}

static void tile_event_cb(lv_event_t *e)
{
    show_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/* Live status lines of the overview tiles (wifi, HA, update, ...). */
static void refresh_tile_status(lv_timer_t *t)
{
    (void)t;
    if (!s_body || s_page) return;
    lv_obj_t *grid = lv_obj_get_child(s_body, 0);
    for (uint32_t i = 0; grid && i < lv_obj_get_child_count(grid); ++i) {
        lv_obj_t *tile = lv_obj_get_child(grid, (int32_t)i);
        lv_obj_t *status = lv_obj_get_child(lv_obj_get_child(tile, 1), 1);
        const settings_tab_t *tab = status ? lv_obj_get_user_data(status) : NULL;
        if (!tab || !tab->status) continue;
        bool ok = false;
        const char *text = tab->status(&ok);
        lv_obj_t *dot = lv_obj_get_child(status, 0);
        lv_obj_t *label = lv_obj_get_child(status, 1);
        if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
        if (ok) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_home(void)
{
    clear_body();
    lv_label_set_text(s_title, tr(TR_SETTINGS_TITLE));
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);

    int tab_count = 0;
    const settings_tab_t **tabs = settings_get_tabs(&tab_count);

    // Two equal columns, rows sized to the tiles.
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[16 + 1];
    int rows = (tab_count + 1) / 2;
    if (rows > 16) rows = 16;
    for (int r = 0; r < rows; r++) row_dsc[r] = TILE_HEIGHT;
    row_dsc[rows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_t *grid = lv_obj_create(s_body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(grid, UI_EDGE, 0);
    lv_obj_set_style_pad_bottom(grid, UI_EDGE, 0);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);
    lv_obj_set_style_pad_column(grid, UI_GAP, 0);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < tab_count && i / 2 < rows; i++) {
        const settings_tab_t *tab = tabs[i];
        if (!tab || !tab->create_page) continue;

        // Overview tile (design guide): icon circle top left, title and
        // status (with the green dot when connected) at the bottom.
        lv_obj_t *tile = lv_obj_create(grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tile, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x1A1A18), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, UI_R_TILE, 0);
        lv_obj_set_style_pad_all(tile, 20, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui_bubble(tile, 64, UI_ICON_M, tab->line_icon ? tab->line_icon : LI_SETTINGS, UI_LOOK_INFO);
        lv_obj_t *texts = ui_box(tile);
        lv_obj_set_size(texts, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 2, 0);
        ui_label_line(texts, UI_FONT_NAME, UI_TEXT, tab->name_key ? tr(tab->name_key) : tab->name ? tab->name : "");
        lv_obj_t *status = ui_box(texts);
        lv_obj_set_size(status, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(status, 8, 0);
        lv_obj_t *dot = ui_box(status);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(UI_OK), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *text = ui_label_line(status, UI_FONT_LABEL, UI_TEXT2, tab->summary_key ? tr(tab->summary_key) : "");
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_user_data(status, (void *)tab);
    }
    refresh_tile_status(NULL);
    s_status_timer = lv_timer_create(refresh_tile_status, 1000, NULL);
}

static void show_tab(int index)
{
    int tab_count = 0;
    const settings_tab_t **tabs = settings_get_tabs(&tab_count);
    if (index < 0 || index >= tab_count || !tabs[index] || !tabs[index]->create_page) return;
    const settings_tab_t *tab = tabs[index];

    clear_body();
    lv_label_set_text(s_title, tab->name_key ? tr(tab->name_key) : tab->name ? tab->name : "");
    lv_obj_remove_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);

    s_page = tab->create_page(s_body);
    if (s_page) {
        apply_touch_style(s_page);
        lv_obj_update_layout(s_page);
        center_short_rows(s_page);
    }
}

// ── Open / close ─────────────────────────────────────────────────────────────
void settings_open(lv_obj_t *parent)
{
    if (s_root != NULL) {
        // Already open
        return;
    }

    // Ensure settings are initialized
    if (!is_initialized) {
        settings_init();
    }

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(s_root, UI_EDGE, 0);
    lv_obj_set_style_pad_row(s_root, UI_GAP, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE); // swallow taps meant for the dashboard below
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Header: [back]  Title  [close]
    lv_obj_t *header = lv_obj_create(s_root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), HEADER_HEIGHT);
    lv_obj_set_style_pad_hor(header, UI_EDGE, 0);
    lv_obj_set_style_pad_column(header, 16, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_back_btn = header_button(header, LI_CHEVRON_LEFT, back_event_cb);
    s_title = lv_label_create(header);
    lv_obj_set_style_text_font(s_title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(s_title, 1);
    lv_obj_set_style_pad_left(s_title, 4, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    header_button(header, LI_X, close_event_cb);

    // Body: tiles (home) or the selected tab's page
    s_body = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_width(s_body, LV_PCT(100));
    lv_obj_set_flex_grow(s_body, 1);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    // Shared on-screen keyboard for all tabs' textareas (see
    // settings_attach_keyboard()); floats over the body, hidden until a
    // textarea is focused.
    settings_keyboard = lv_keyboard_create(s_root);
    lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(settings_keyboard, LV_PCT(100), LV_PCT(45));
    lv_obj_align(settings_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(settings_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
    ui_style_keyboard(settings_keyboard);

    show_home();
    printf("[Settings] Opened\n");
}

static bool s_reload_dashboard;

void settings_reload_dashboard_on_close(void) { s_reload_dashboard = true; }

void settings_close(void)
{
    if (s_reload_dashboard) {
        s_reload_dashboard = false;
        dashboard_show_default(); /* async: after the settings are gone */
    }
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    if (s_root) {
        lv_obj_delete(s_root); // keyboard, header and pages are children
        s_root = NULL;
        s_back_btn = NULL;
        s_title = NULL;
        s_body = NULL;
        s_page = NULL;
        settings_keyboard = NULL;
    }
    printf("[Settings] Closed\n");
}

bool settings_is_open(void)
{
    return s_root != NULL;
}

void settings_dropdown_show_cut(lv_obj_t *dd, char *buf, size_t size)
{
    char name[96];
    lv_dropdown_get_selected_str(dd, name, sizeof(name));
    const lv_font_t *font = lv_obj_get_style_text_font(dd, LV_PART_MAIN);
    lv_obj_update_layout(dd);
    const int32_t room = lv_obj_get_content_width(dd) - 36; /* the arrow */
    snprintf(buf, size, "%s", name);
    size_t len = strlen(buf);
    lv_point_t text;
    lv_text_get_size(&text, buf, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    while (len > 1 && text.x > room) {
        do {
            len--;
        } while (len > 0 && (buf[len] & 0xC0) == 0x80); /* whole UTF-8 characters */
        snprintf(buf + len, size - len, "…");
        lv_text_get_size(&text, buf, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }
    lv_dropdown_set_text(dd, buf);
}
