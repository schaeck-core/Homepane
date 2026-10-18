/**
 * @file addon.c
 * @brief Doorbell addon implementation
 *
 * Entry point for the Doorbell integration.
 * Shows camera stream when doorbell is pressed and allows door unlock.
 */

#include <stdio.h>
#include <string.h>
#include "addon.h"
#include "settings.h"
#include "../addons.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"

static bool is_initialized = false;

void addon_doorbell_init(void)
{
    if (is_initialized) return;

    printf("[Doorbell] Initializing addon...\n");


    // The doorbell itself (watching the trigger, the overlay, opening) is
    // app/cpp/doorbell + app/cpp/ui/DoorbellOverlay, attached to the Home
    // Assistant connection by app_cpp_dashboard_render().

    is_initialized = true;
    printf("[Doorbell] Addon initialized\n");
}

void addon_doorbell_deinit(void)
{
    if (!is_initialized) return;

    printf("[Doorbell] Deinitializing addon...\n");


    is_initialized = false;
}

void addon_doorbell_show_camera(void)
{
    app_cpp_doorbell_test(); // the overlay, as if it rang
}

void addon_doorbell_unlock(void)
{
    // Opening is the overlay's hold-to-open button (DoorbellController::open()).
}

static const addon_t doorbell_addon = {
    .name = "Doorbell",
    .id = "doorbell",
    .description = "Doorbell camera and door unlock integration",
    .init = addon_doorbell_init,
    .deinit = addon_doorbell_deinit,
    .enabled = true
};

void addon_doorbell_register(void)
{
    addons_register(&doorbell_addon);
}

const addon_t* addon_doorbell_get(void)
{
    return &doorbell_addon;
}
