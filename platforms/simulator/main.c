#define _DEFAULT_SOURCE /* needed for usleep() */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "lvgl/lvgl.h"
#include "glob.h"
#ifndef ESP_PLATFORM
#include <microhttpd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include "config.h"
#include "app/i18n.h"
#include "websocket_client.h"
#include "app/cpp/app_facade.h"
#include "sim_script.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_display_t *hal_init(int32_t w, int32_t h);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

#ifndef ESP_PLATFORM
extern void freertos_main(void);
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      VARIABLES
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * loadingOverlay;
bool isLoading = false;

// enter_init_state()/enter_setup_state()/enter_complete_state() and the
// shared render_content()/AppState/fontBig/fontSmall logic that used to
// live here moved to app/cpp/application/AppStateController.cpp
// (C++ migration phase 11) — see app_cpp_enter_initial_state()
// in main() below.

int main(int argc, char **argv)
{
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /* C++ migration phase 1: construct the C++ Application skeleton. */
  app_cpp_create();

  /*Initialize LVGL*/
  lv_init();
  //lv_png_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
#ifndef ESP_PLATFORM
  /* HOMEPANE_RESOLUTION=WxH: other panel sizes (default the board's 720x720) */
  int width = 720, height = 720;
  const char *resolution = getenv("HOMEPANE_RESOLUTION");
  if (resolution && sscanf(resolution, "%dx%d", &width, &height) != 2) width = height = 720;
  lv_display_t *disp = hal_init(width, height);
  /* HOMEPANE_MONITORS=0: without the FPS/memory numbers in the corners
     (screenshots for the docs) */
  const char *monitors = getenv("HOMEPANE_MONITORS");
  if (monitors && monitors[0] == '0') {
    lv_sysmon_hide_performance(disp);
    lv_sysmon_hide_memory(disp);
  }
  sim_script_start(disp);
#endif

  // Desktop-specific: render shared content after window/display is ready
  loadConfig();
  { /* UI language (app/i18n); German until the user picks another */
      const char *lang = getConfig("language");
      i18n_set_language(lang && lang[0] ? lang : "de");
  }
  app_cpp_enter_initial_state();

  app_cpp_start();




  /*

  //Create a white label, set its text and align it to the center
  lv_obj_t *btn = lv_btn_create(lv_screen_active());                   // Create a button on the active screen
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);                            // Center the button
  lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_CLICKED, NULL); // Register click event handler
  lv_obj_t *label = lv_label_create(btn);                              // Create label on the button
  lv_label_set_text(label, "Send Message");
  */
  /*
  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello world");
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  */

  while (1)
  {
#ifndef ESP_PLATFORM
    ws_service_all();
    app_cpp_poll();
    lv_timer_handler();
    usleep(1 * 1000);
#else
    /* ESP handles LVGL in its own task loop; exit here */
    break;
#endif
  }




  app_cpp_stop();
  releaseConfig();

#ifndef ESP_PLATFORM
  return 0;
#else
  return 0;
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Initialize the Hardware Abstraction Layer (HAL) for the LVGL graphics
 * library
 */
static lv_display_t *hal_init(int32_t w, int32_t h)
{
#ifndef ESP_PLATFORM
  // Deliberately no default lv_group / lv_indev_set_group here (unlike
  // the LVGL SDL example template this was originally based on): this
  // app is touch-only on real hardware (ESP32-P4's app_main.c sets up no
  // group either) and the on-screen keyboard widgets
  // (app/views/initialization.c, app/views/network_setup.c) wire
  // themselves to a textarea directly via lv_keyboard_set_textarea(),
  // independent of groups. A default group here previously caused a real
  // bug: lv_slider_create()/lv_button_create() both set
  // LV_OBJ_FLAG_SCROLL_ON_FOCUS by default, and LVGL auto-adds every new
  // slider/button to the default group (lv_obj_class_init_obj) and
  // auto-focuses the first one added to an empty group — so the first
  // slider/button anywhere in a freshly-built dashboard (e.g. a
  // ThermostatWidget arc or a LightWidget's modal slider) silently
  // fired an animated lv_obj_scroll_to_view_recursive() that dragged the
  // dashboard's Y-snapping page down by exactly one row on load, with no
  // user interaction. Confirmed by instrumenting DashboardSession's page
  // with an LV_EVENT_ALL logger: scroll_y eased from 0 to 330 (one row)
  // shortly after construction, driven entirely by LV_EVENT_FOCUSED.

  lv_display_t *disp = lv_sdl_window_create(w, h);
  //lv_display_set_antialiasing(disp, true);
  lv_display_set_dpi(disp, 255);
  //lv_sdl_window_set_zoom(disp, 0.8f);

  lv_indev_t *mouse = lv_sdl_mouse_create();
  lv_indev_set_display(mouse, disp);
  lv_display_set_default(disp);
  //lv_display_set_antialiasing(disp, true);

  lv_indev_t *mousewheel = lv_sdl_mousewheel_create();
  lv_indev_set_display(mousewheel, disp);

  lv_indev_t *kb = lv_sdl_keyboard_create();
  lv_indev_set_display(kb, disp);


  return disp;
#else
  (void)w; (void)h;
  return NULL;
#endif
}
