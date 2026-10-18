/**
 * LVGL Configuration for ESP32-P4
 * Minimal config for Waveshare ESP32-P4-86-Panel-ETH-2RO
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16bit (RGB565) for typical LCD panels */
#define LV_COLOR_DEPTH 16

/* Display resolution (adjust to your actual panel, e.g., 1024x600 or 800x480) */
#define LV_HOR_RES_MAX 1024
#define LV_VER_RES_MAX 600

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128 * 1024U)  /* 128KB for LVGL heap */

/* Display refresh settings */
#define LV_DISP_DEF_REFR_PERIOD 30  /* ms */

/* Input device read period */
#define LV_INDEV_DEF_READ_PERIOD 30  /* ms */

/* Enable needed features */
#define LV_USE_SYSMON   1
#if LV_USE_SYSMON
    /** Get the idle percentage. E.g. uint32_t my_get_idle(void); */
    #define LV_SYSMON_GET_IDLE lv_os_get_idle_percent

    /** 1: Show CPU usage and FPS count.
     *  - Requires `LV_USE_SYSMON = 1` */
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
    #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

        /** 0: Displays performance data on the screen; 1: Prints performance data using log. */
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    /** 1: Show used memory and memory fragmentation.
     *     - Requires `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN`
     *     - Requires `LV_USE_SYSMON = 1`*/
    #define LV_USE_MEM_MONITOR 1
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif
#endif /*LV_USE_SYSMON*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Font and rendering */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Widgets to enable (add as needed) */
#define LV_USE_QRCODE 1
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_IMG 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_CHART 1
#define LV_USE_TEXTAREA 1

/* Demos and examples (disable for production) */
#define LV_BUILD_EXAMPLES 0

/* ---- Rendering extras ---- */
/* Enable radial/conical gradient support for the light glow effect */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1

/* Enable vector graphics and LVGL's built-in SVG decoder for the boot logo */
#define LV_USE_VECTOR_GRAPHIC  1
#define LV_USE_SVG             1
#define LV_USE_SVG_ANIMATION   0
#define LV_USE_SVG_DEBUG       0

#endif /* LV_CONF_H */
