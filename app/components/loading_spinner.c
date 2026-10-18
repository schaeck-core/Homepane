#include "loading_spinner.h"
#include <math.h>

#define SPINNER_SIZE 120
#define DOT_SIZE 16
#define DOT_COUNT 3
#define CIRCLE_RADIUS 35
#define ROTATION_TIME 2000  // 2 seconds for full rotation
#define PULSE_TIME 1000     // 1 second for pulse effect

// Private structure to store spinner data
typedef struct {
    lv_obj_t* dots[DOT_COUNT];
    lv_obj_t* label;
    lv_obj_t* container;
} spinner_data_t;

// Animation callback for rotation
static void rotate_anim_cb(void* var, int32_t value) {
    spinner_data_t* data = (spinner_data_t*)lv_obj_get_user_data(var);
    if(!data) return;

    // Update position of each dot
    for(int i = 0; i < DOT_COUNT; i++) {
        if(!data->dots[i]) continue;

        // Calculate angle with offset for each dot
        float angle = (value + (i * 120)) * M_PI / 180.0f;

        int x = (SPINNER_SIZE / 2) + (CIRCLE_RADIUS * cos(angle)) - (DOT_SIZE / 2);
        int y = (SPINNER_SIZE / 2) + (CIRCLE_RADIUS * sin(angle)) - (DOT_SIZE / 2);

        lv_obj_set_pos(data->dots[i], x, y);
    }
}

// Animation callback for pulsing effect
static void pulse_anim_cb(void* var, int32_t value) {
    lv_obj_t* dot = (lv_obj_t*)var;
    if(!dot) return;

    // Scale the dot size (8-16px)
    int size = DOT_SIZE - 6 + (value * 6 / 255);
    lv_obj_set_size(dot, size, size);
}

lv_obj_t* loading_spinner_create(lv_obj_t* parent, const char* text) {
    if(!parent) parent = lv_screen_active();

    // Create overlay
    lv_obj_t* overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_30, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Create spinner container
    lv_obj_t* spinner_cont = lv_obj_create(overlay);
    lv_obj_remove_style_all(spinner_cont);
    lv_obj_set_size(spinner_cont, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_center(spinner_cont);
    lv_obj_clear_flag(spinner_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Allocate and store spinner data
    spinner_data_t* data = lv_malloc(sizeof(spinner_data_t));
    if(!data) return overlay;

    data->container = spinner_cont;
    data->label = NULL;
    lv_obj_set_user_data(overlay, data);

    // Create animated dots with gradient colors
    const lv_color_t colors[DOT_COUNT] = {
        lv_color_hex(0x00D9FF),  // Cyan
        lv_color_hex(0x0078D4),  // Blue
        lv_color_hex(0x7B68EE),  // Purple
    };

    for(int i = 0; i < DOT_COUNT; i++) {
        lv_obj_t* dot = lv_obj_create(spinner_cont);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, colors[i], 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

        // Add shadow for depth
        lv_obj_set_style_shadow_width(dot, 10, 0);
        lv_obj_set_style_shadow_color(dot, colors[i], 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
        lv_obj_set_style_shadow_spread(dot, 2, 0);

        data->dots[i] = dot;

        // Create pulse animation for each dot (staggered)
        lv_anim_t pulse_anim;
        lv_anim_init(&pulse_anim);
        lv_anim_set_var(&pulse_anim, dot);
        lv_anim_set_exec_cb(&pulse_anim, pulse_anim_cb);
        lv_anim_set_values(&pulse_anim, 0, 255);
        lv_anim_set_time(&pulse_anim, PULSE_TIME);
        lv_anim_set_playback_time(&pulse_anim, PULSE_TIME);
        lv_anim_set_delay(&pulse_anim, i * (PULSE_TIME / DOT_COUNT));
        lv_anim_set_repeat_count(&pulse_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&pulse_anim);
    }

    // Create rotation animation
    lv_anim_t rotate_anim;
    lv_anim_init(&rotate_anim);
    lv_anim_set_var(&rotate_anim, overlay);
    lv_anim_set_exec_cb(&rotate_anim, rotate_anim_cb);
    lv_anim_set_values(&rotate_anim, 0, 360);
    lv_anim_set_time(&rotate_anim, ROTATION_TIME);
    lv_anim_set_repeat_count(&rotate_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&rotate_anim);

    // Create text label if provided
    if(text) {
        lv_obj_t* label = lv_label_create(overlay);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_align_to(label, spinner_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
        data->label = label;
    }

    return overlay;
}

void loading_spinner_delete(lv_obj_t* overlay) {
    if(!overlay) return;

    // Get and free spinner data
    spinner_data_t* data = (spinner_data_t*)lv_obj_get_user_data(overlay);
    if(data) {
        // Delete animations
        for(int i = 0; i < DOT_COUNT; i++) {
            if(data->dots[i]) {
                lv_anim_delete(data->dots[i], NULL);
            }
        }
        lv_anim_delete(overlay, NULL);

        lv_free(data);
    }

    // Delete the overlay object (this also deletes all children)
    lv_obj_del(overlay);
}

void loading_spinner_set_text(lv_obj_t* overlay, const char* text) {
    if(!overlay) return;

    spinner_data_t* data = (spinner_data_t*)lv_obj_get_user_data(overlay);
    if(!data) return;

    if(text) {
        if(!data->label) {
            // Create label if it doesn't exist
            data->label = lv_label_create(overlay);
            lv_obj_set_style_text_color(data->label, lv_color_white(), 0);
            lv_obj_set_style_text_font(data->label, &lv_font_montserrat_16, 0);
            lv_obj_align_to(data->label, data->container, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
        }
        lv_label_set_text(data->label, text);
    } else if(data->label) {
        // Remove label if text is NULL
        lv_obj_del(data->label);
        data->label = NULL;
    }
}
