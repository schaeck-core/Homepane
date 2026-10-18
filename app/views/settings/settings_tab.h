/**
 * @file settings_tab.h
 * @brief Interface for registering settings tabs
 *
 * This header defines the interface that all settings tabs must implement.
 * Each settings module can register its own tab by implementing the
 * settings_tab_t structure and calling settings_register_tab().
 */

#ifndef SETTINGS_TAB_H
#define SETTINGS_TAB_H

#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../i18n_keys.h"

/**
 * @brief Settings tab descriptor
 *
 * Each settings module provides one of these structures to define
 * how its settings page should be created and displayed.
 */
typedef struct {
    const char *name;           /**< Display name for the tab (shown in sidebar) */
    /**
     * @brief Create the settings page content
     * @param parent Container to build the page in; use settings_page_create(parent)
     * @return The created page object
     */
    lv_obj_t* (*create_page)(lv_obj_t *parent);
    /* Overview tile (design guide): Lucide icon (LI_*), a short summary,
     * and optionally a live status that replaces the summary; *ok = true
     * shows the green status dot. All optional. */
    const char *line_icon;
    tr_key_t name_key;    /* the shown name (0 = use .name) */
    tr_key_t summary_key; /* 0 = none */
    const char *(*status)(bool *ok);
} settings_tab_t;

/**
 * @brief Register a settings tab
 * @param tab Pointer to the tab descriptor (must remain valid)
 */
void settings_register_tab(const settings_tab_t *tab);

/**
 * @brief Get all registered tabs
 * @param count Output parameter for the number of tabs
 * @return Array of registered tab pointers
 */
const settings_tab_t** settings_get_tabs(int *count);

/**
 * @brief Clear all registered tabs (for cleanup)
 */
void settings_clear_tabs(void);

#endif // SETTINGS_TAB_H
