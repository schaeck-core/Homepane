/**
 * @file settings.h
 * @brief Doorbell settings header
 */

#ifndef ADDON_DOORBELL_SETTINGS_H
#define ADDON_DOORBELL_SETTINGS_H

#include "../../views/settings/settings_tab.h"

/**
 * @brief Initialize and register the Doorbell settings tab
 */
void addon_doorbell_settings_init(void);

/**
 * @brief Get the Doorbell settings tab descriptor
 */
const settings_tab_t* addon_doorbell_settings_get_tab(void);

#endif // ADDON_DOORBELL_SETTINGS_H
