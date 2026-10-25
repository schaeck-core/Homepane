/* The start screen (design canvas "Startscreen (animiert)"): the logo's
 * panes appear one by one, the lit one last, then the wordmark, then the
 * progress of the start (Wi-Fi, Home Assistant, dashboard). It lives on the
 * top layer, so it stays while the screen below changes from the Wi-Fi wait
 * to the dashboard, and goes once the dashboard is ready. Only for the
 * first start: later dashboard rebuilds keep their own spinner. UI thread. */
#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include "lvgl/lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_STEP_WIFI,      /* waiting for the network */
    BOOT_STEP_HA,        /* connecting to Home Assistant */
    BOOT_STEP_DASHBOARD, /* connected, the dashboard loads */
} boot_step_t;

/* Shows it (with the intro animation) unless it is shown or was finished. */
void boot_screen_show(void);
/* The shown step: status text and progress. From BOOT_STEP_HA on it moves
 * to BOOT_STEP_DASHBOARD by itself once Home Assistant is connected. */
void boot_screen_step(boot_step_t step);
/* The start went wrong: the text in the warning color, the progress goes,
 * and a button for `on_button` (the start screen is gone when it runs). */
void boot_screen_error(const char *text, const char *button, void (*on_button)(void));
/* The dashboard is ready: a full sand bar for a moment, then it goes. */
void boot_screen_finish(void);
/* Gone at once (an error screen takes over). */
void boot_screen_hide(void);
/* Shown now, or not finished yet: the first dashboard is still to come. */
bool boot_screen_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_SCREEN_H */
