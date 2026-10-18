/**
 * @file addon.h
 * @brief Doorbell addon header
 */

#ifndef ADDON_DOORBELL_H
#define ADDON_DOORBELL_H

#include "../addons.h"

/**
 * @brief Register the Doorbell addon
 */
void addon_doorbell_register(void);

/**
 * @brief Initialize the Doorbell addon
 */
void addon_doorbell_init(void);

/**
 * @brief Deinitialize the Doorbell addon
 */
void addon_doorbell_deinit(void);

/**
 * @brief Get the Doorbell addon descriptor
 */
const addon_t* addon_doorbell_get(void);

/**
 * @brief Trigger the doorbell camera view
 * Called when the configured trigger entity is activated
 */
void addon_doorbell_show_camera(void);

/**
 * @brief Trigger the unlock action
 */
void addon_doorbell_unlock(void);

#endif // ADDON_DOORBELL_H
