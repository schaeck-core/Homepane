#include "error_handler.h"
#include "lvgl/lvgl.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <execinfo.h>
#include <unistd.h>

static bool critical_error_occurred = false;
static lv_obj_t *error_overlay = NULL;
static jmp_buf recovery_point;
static bool recovery_point_set = false;

// Hilfsfunktion für Backtrace (nur auf Unix-Systemen)
static void print_backtrace(void) {
    void *array[10];
    size_t size;
    char **strings;

    size = backtrace(array, 10);
    strings = backtrace_symbols(array, size);

    printf("\n=== Stack Trace ===\n");
    for (size_t i = 0; i < size; i++) {
        printf("%s\n", strings[i]);
    }
    printf("===================\n\n");

    free(strings);
}

// Callback-Funktion für Close-Button
static void close_button_cb(lv_event_t *e) {
    lv_obj_t *overlay = lv_event_get_user_data(e);
    if (overlay) {
        lv_obj_del(overlay);
        error_overlay = NULL;
    }
}

// Signal Handler für kritische Fehler
static void signal_handler(int sig) {
    const char *signal_name = "Unknown";
    const char *description = "An unexpected error occurred";

    switch(sig) {
        case SIGSEGV:
            signal_name = "Segmentation Fault";
            description = "Invalid memory access detected.\nThe application tried to access memory it shouldn't.";
            break;
        case SIGFPE:
            signal_name = "Floating Point Exception";
            description = "Mathematical error detected.\n(Division by zero or invalid operation)";
            break;
        case SIGILL:
            signal_name = "Illegal Instruction";
            description = "Invalid CPU instruction detected.";
            break;
        case SIGABRT:
            signal_name = "Abort Signal";
            description = "Application was aborted.\n(Possible assertion failure)";
            break;
        case SIGBUS:
            signal_name = "Bus Error";
            description = "Hardware memory access error detected.";
            break;
    }

    printf("\n!!! CRITICAL ERROR !!!\n");
    printf("Signal: %s (%d)\n", signal_name, sig);
    printf("Description: %s\n", description);

    print_backtrace();

    critical_error_occurred = true;

    // Zeige Fehler-UI
    char error_msg[512];
    snprintf(error_msg, sizeof(error_msg),
             "Signal: %s\n\n%s\n\nThe application will attempt to continue,\nbut may be unstable.",
             signal_name, description);

    show_error_message("Critical Error", error_msg);

    // Versuche zur Recovery-Position zurückzukehren
    if (recovery_point_set) {
        longjmp(recovery_point, 1);
    } else {
        // Wenn kein Recovery-Point gesetzt ist, können wir nur loggen und hoffen
        printf("No recovery point set - attempting to continue...\n");
        signal(sig, signal_handler); // Re-install handler
    }
}

void error_handler_init(void) {
    // Registriere Signal-Handler für verschiedene kritische Fehler
    signal(SIGSEGV, signal_handler);  // Segmentation Fault
    signal(SIGFPE, signal_handler);   // Floating Point Exception
    signal(SIGILL, signal_handler);   // Illegal Instruction
    signal(SIGABRT, signal_handler);  // Abort
    signal(SIGBUS, signal_handler);   // Bus Error

    printf("Error handler initialized - monitoring for critical errors\n");
}

void show_error_message(const char *title, const char *message) {
    // Entferne altes Overlay falls vorhanden
    if (error_overlay != NULL) {
        lv_obj_del(error_overlay);
        error_overlay = NULL;
    }

    // Erstelle Fehler-Overlay
    error_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(error_overlay);
    lv_obj_set_size(error_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(error_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(error_overlay, LV_OPA_80, 0);
    lv_obj_clear_flag(error_overlay, LV_OBJ_FLAG_CLICKABLE);

    // Fehler-Box (Container)
    lv_obj_t *error_box = lv_obj_create(error_overlay);
    lv_obj_set_size(error_box, 500, LV_SIZE_CONTENT);
    lv_obj_center(error_box);
    lv_obj_set_style_bg_color(error_box, lv_color_hex(0x2c2c2c), 0);
    lv_obj_set_style_border_color(error_box, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_border_width(error_box, 3, 0);
    lv_obj_set_style_radius(error_box, 10, 0);
    lv_obj_set_style_pad_all(error_box, 20, 0);
    lv_obj_set_flex_flow(error_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(error_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(error_box, 15, 0);

    // Icon (⚠️ oder ❌)
    lv_obj_t *icon = lv_label_create(error_box);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);

    // Titel
    lv_obj_t *title_label = lv_label_create(error_box);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);

    // Nachricht
    lv_obj_t *msg_label = lv_label_create(error_box);
    lv_label_set_text(msg_label, message);
    lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_label, LV_PCT(100));

    // Close Button
    lv_obj_t *close_btn = lv_btn_create(error_box);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xff4444), 0);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_label_set_text(btn_label, "Close");
    lv_obj_center(btn_label);

    // Close-Button Event
    lv_obj_add_event_cb(close_btn, close_button_cb, LV_EVENT_CLICKED, error_overlay);

    printf("Error message displayed: %s - %s\n", title, message);
}

bool has_critical_error(void) {
    return critical_error_occurred;
}

// Setze einen Recovery-Point (für fortgeschrittene Fehlerbehandlung)
// Diese Funktion kann in der Main-Loop verwendet werden
int error_handler_set_recovery_point(void) {
    recovery_point_set = true;
    return setjmp(recovery_point);
}
