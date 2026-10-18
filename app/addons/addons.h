/**
 * @file addons.h
 * @brief Addon system loader
 *
 * The addon system allows modular extensions to be loaded dynamically.
 * Each addon resides in its own directory under src/addons/ and provides:
 * - addon.c/h: Entry point with init function
 * - settings.c/h: Settings page for the addon
 */

#ifndef ADDONS_H
#define ADDONS_H

#include "lvgl/lvgl.h"

/**
 * @brief Addon descriptor structure
 */
typedef struct {
    const char *name;           // Display name of the addon
    const char *id;             // Unique identifier (directory name)
    const char *description;    // Brief description
    void (*init)(void);         // Initialization function
    void (*deinit)(void);       // Cleanup function (optional)
    bool enabled;               // Whether the addon is enabled
} addon_t;

/**
 * @brief Initialize the addon system and load all addons
 */
void addons_init(void);

/**
 * @brief Register the settings tabs of the enabled addons (the settings
 * call it after clearing their tab list; addons may have started before).
 */
void addons_register_settings(void);

/**
 * @brief Deinitialize the addon system
 */
void addons_deinit(void);

/**
 * @brief Get the list of registered addons
 * @param count Pointer to store the number of addons
 * @return Array of addon pointers
 */
const addon_t** addons_get_list(int *count);

/**
 * @brief Register an addon
 * @param addon Pointer to addon descriptor
 */
void addons_register(const addon_t *addon);

/**
 * @brief Check if an addon is enabled
 * @param addon_id The addon identifier
 * @return true if enabled, false otherwise
 */
bool addons_is_enabled(const char *addon_id);

/**
 * @brief Enable or disable an addon
 * @param addon_id The addon identifier
 * @param enabled Whether to enable or disable
 */
void addons_set_enabled(const char *addon_id, bool enabled);

#endif // ADDONS_H
