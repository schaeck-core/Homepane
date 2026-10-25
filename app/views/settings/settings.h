/**
 * @file settings.h
 * @brief Settings menu view
 */

#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include "lvgl/lvgl.h"

/**
 * @brief Initialize the settings system and register all built-in tabs
 */
void settings_init(void);

/**
 * @brief Open the settings menu as an overlay
 * @param parent The parent object (usually lv_screen_active())
 */
void settings_open(lv_obj_t *parent);

/**
 * @brief Close the settings menu
 */
void settings_close(void);

/* The dashboard's settings changed (default dashboard, mode, rows): it is
 * rebuilt with the default dashboard when the settings close. */
void settings_reload_dashboard_on_close(void);

/**
 * @brief Check if settings menu is currently open
 * @return true if open, false otherwise
 */
bool settings_is_open(void);

/**
 * @brief Wire a textarea into the settings menu's shared on-screen keyboard.
 *
 * Call once per textarea, right after creating it (in addition to whatever
 * LV_EVENT_DEFOCUSED handler the caller already registers to persist the
 * value — this only adds show/hide-on-focus behavior, it doesn't replace
 * anything). Must be called while the settings menu is open, i.e. from a
 * tab's create_page().
 *
 * @param textarea The textarea to attach keyboard behavior to
 */
void settings_attach_keyboard(lv_obj_t *textarea);

/**
 * @brief Create a tab's page inside the settings body (scrollable column).
 *
 * Call from a tab's create_page(parent) and return the result. Stock
 * controls added to the page (switches, dropdowns, textareas) are enlarged
 * for touch automatically after create_page() returns.
 */
lv_obj_t *settings_page_create(lv_obj_t *parent);

/**
 * @brief Create one setting row (card) on a page: horizontal flex row,
 *        finger-sized minimum height. Typical content: icon label, text
 *        label with flex-grow 1, control.
 */
lv_obj_t *settings_row_create(lv_obj_t *page);

/* A row with an icon and a name; its control goes after them. */
lv_obj_t *settings_icon_row(lv_obj_t *page, const char *icon, const char *text);
/* A dropdown in a row, in the settings' look. */
lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options);
/* A row that opens something (a chevron at its end); `cb` on a tap. */
lv_obj_t *settings_link_row(lv_obj_t *page, const char *icon, const char *text, lv_event_cb_t cb);

/**
 * @brief Show a dropdown's selected option cut with "…" to fit next to its
 *        arrow (LVGL draws long text under it). Call after a selection
 *        changes; buf must outlive the dropdown (lv_dropdown_set_text
 *        keeps the pointer).
 */
void settings_dropdown_show_cut(lv_obj_t *dd, char *buf, size_t size);

#endif // SETTINGS_VIEW_H
