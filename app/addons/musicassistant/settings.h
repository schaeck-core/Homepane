/**
 * @file settings.h
 * @brief Music Assistant settings header
 */

#ifndef ADDON_MUSICASSISTANT_SETTINGS_H
#define ADDON_MUSICASSISTANT_SETTINGS_H

#include "../../views/settings/settings_tab.h"

/**
 * @brief Initialize and register the Music Assistant settings tab
 */
void addon_musicassistant_settings_init(void);

/**
 * @brief Get the Music Assistant settings tab descriptor
 */
const settings_tab_t* addon_musicassistant_settings_get_tab(void);

#endif // ADDON_MUSICASSISTANT_SETTINGS_H
