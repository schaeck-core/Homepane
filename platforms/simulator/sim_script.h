#ifndef PLATFORMS_SIMULATOR_SIM_SCRIPT_H
#define PLATFORMS_SIMULATOR_SIM_SCRIPT_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the steps in $HOMEPANE_SCRIPT (see sim_script.c); no-op without it. */
void sim_script_start(lv_display_t *disp);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORMS_SIMULATOR_SIM_SCRIPT_H */
