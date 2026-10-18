/**
 * @file haptic.c
 * @brief Haptic Feedback Implementation
 *
 * This module provides force touch detection and haptic feedback
 * using piezo sensors and DRV2605 haptic driver.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "haptic.h"

// ============================================================================
// DRV2605 Register Definitions
// ============================================================================

#define DRV2605_REG_STATUS          0x00
#define DRV2605_REG_MODE            0x01
#define DRV2605_REG_RTP             0x02
#define DRV2605_REG_LIBRARY         0x03
#define DRV2605_REG_WAVESEQ1        0x04
#define DRV2605_REG_WAVESEQ2        0x05
#define DRV2605_REG_WAVESEQ3        0x06
#define DRV2605_REG_WAVESEQ4        0x07
#define DRV2605_REG_WAVESEQ5        0x08
#define DRV2605_REG_WAVESEQ6        0x09
#define DRV2605_REG_WAVESEQ7        0x0A
#define DRV2605_REG_WAVESEQ8        0x0B
#define DRV2605_REG_GO              0x0C
#define DRV2605_REG_OVERDRIVE       0x0D
#define DRV2605_REG_SUSTAIN_POS     0x0E
#define DRV2605_REG_SUSTAIN_NEG     0x0F
#define DRV2605_REG_BRAKE           0x10
#define DRV2605_REG_A2V_CTRL        0x11
#define DRV2605_REG_A2V_MIN_IN      0x12
#define DRV2605_REG_A2V_MAX_IN      0x13
#define DRV2605_REG_A2V_MIN_OUT     0x14
#define DRV2605_REG_A2V_MAX_OUT     0x15
#define DRV2605_REG_RATED_VOLTAGE   0x16
#define DRV2605_REG_OD_CLAMP        0x17
#define DRV2605_REG_A_CAL_COMP      0x18
#define DRV2605_REG_A_CAL_BEMF      0x19
#define DRV2605_REG_FEEDBACK        0x1A
#define DRV2605_REG_CONTROL1        0x1B
#define DRV2605_REG_CONTROL2        0x1C
#define DRV2605_REG_CONTROL3        0x1D
#define DRV2605_REG_CONTROL4        0x1E
#define DRV2605_REG_VBAT            0x21
#define DRV2605_REG_LRA_PERIOD      0x22

// Mode bits
#define DRV2605_MODE_INTERNAL       0x00
#define DRV2605_MODE_EXT_EDGE       0x01
#define DRV2605_MODE_EXT_LEVEL      0x02
#define DRV2605_MODE_PWM            0x03
#define DRV2605_MODE_AUDIO          0x04
#define DRV2605_MODE_RTP            0x05
#define DRV2605_MODE_DIAG           0x06
#define DRV2605_MODE_AUTOCAL        0x07
#define DRV2605_MODE_STANDBY        0x40

// Feedback control bits
#define DRV2605_FB_ERM              0x00
#define DRV2605_FB_LRA              0x80

// Library selection
#define DRV2605_LIB_EMPTY           0x00
#define DRV2605_LIB_ERM_A           0x01
#define DRV2605_LIB_ERM_B           0x02
#define DRV2605_LIB_ERM_C           0x03
#define DRV2605_LIB_ERM_D           0x04
#define DRV2605_LIB_ERM_E           0x05
#define DRV2605_LIB_LRA             0x06

// ============================================================================
// Platform Abstraction
// ============================================================================

// Define platform-specific I2C and GPIO functions
#ifdef ESP_PLATFORM
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "driver/i2c_master.h"
    #include "driver/gpio.h"
    #include "esp_adc/adc_oneshot.h"
    #define PLATFORM_ESP32 1
#else
    // Desktop simulation - stub functions
    #define PLATFORM_DESKTOP 1
#endif

// ============================================================================
// Module State
// ============================================================================

typedef struct {
    bool initialized;
    bool enabled;

    // DRV2605 config
    haptic_drv2605_config_t drv_config;

    // Force touch config
    haptic_force_config_t force_config;
    bool force_touch_enabled;

    // Current state
    uint16_t last_force_value;
    bool force_pressed;
    uint32_t force_press_time;

#ifdef PLATFORM_ESP32
    adc_oneshot_unit_handle_t adc_handle;
    i2c_master_dev_handle_t  i2c_dev_handle;  // NULL until haptic_init with bus handle
#endif
} haptic_state_t;

static haptic_state_t g_haptic = {0};

// ============================================================================
// Platform I2C Implementation
// ============================================================================

#ifdef PLATFORM_ESP32

static bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value) {
    (void)addr;
    if (!g_haptic.i2c_dev_handle) return false;
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(g_haptic.i2c_dev_handle,
                               buf, 2, pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value) {
    (void)addr;
    if (!g_haptic.i2c_dev_handle) return false;
    return i2c_master_transmit_receive(g_haptic.i2c_dev_handle,
                                       &reg, 1, value, 1,
                                       pdMS_TO_TICKS(100)) == ESP_OK;
}

static uint16_t read_piezo_adc(void) {
    int raw = 0;
    adc_oneshot_read(g_haptic.adc_handle, g_haptic.force_config.piezo_pin, &raw);
    return (uint16_t)raw;
}

#else

// Desktop stubs
static bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value) {
    (void)addr; (void)reg; (void)value;
    printf("[Haptic Sim] I2C write: addr=0x%02X reg=0x%02X val=0x%02X\n", addr, reg, value);
    return true;
}

static bool i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value) {
    (void)addr; (void)reg;
    *value = 0;
    return true;
}

static uint16_t read_piezo_adc(void) {
    // Simulate force touch with mouse click pressure
    return 0;
}

#endif

// ============================================================================
// DRV2605 Driver Functions
// ============================================================================

bool haptic_drv2605_write_reg(uint8_t reg, uint8_t value) {
    if (!g_haptic.initialized) return false;
    return i2c_write_byte(g_haptic.drv_config.i2c_addr, reg, value);
}

bool haptic_drv2605_read_reg(uint8_t reg, uint8_t *value) {
    if (!g_haptic.initialized) return false;
    return i2c_read_byte(g_haptic.drv_config.i2c_addr, reg, value);
}

void haptic_drv2605_set_mode(haptic_mode_t mode) {
    haptic_drv2605_write_reg(DRV2605_REG_MODE, (uint8_t)mode);
}

static bool drv2605_init(const haptic_drv2605_config_t *config) {
    // Enable pin if configured
#ifdef PLATFORM_ESP32
    if (config->en_pin != 0xFF) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config->en_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(config->en_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif

    // Exit standby mode
    if (!haptic_drv2605_write_reg(DRV2605_REG_MODE, 0x00)) {
        printf("[Haptic] Failed to communicate with DRV2605\n");
        return false;
    }

    // Set actuator type feedback control
    uint8_t fb_reg = (config->actuator_type == HAPTIC_ACTUATOR_LRA) ?
                      DRV2605_FB_LRA : DRV2605_FB_ERM;
    // Add default brake factor and loop gain
    fb_reg |= 0x36; // BEMF gain, brake factor
    haptic_drv2605_write_reg(DRV2605_REG_FEEDBACK, fb_reg);

    // Set rated voltage
    haptic_drv2605_write_reg(DRV2605_REG_RATED_VOLTAGE, config->rated_voltage);

    // Set overdrive voltage clamp
    haptic_drv2605_write_reg(DRV2605_REG_OD_CLAMP, config->overdrive_voltage);

    // Select library (ERM or LRA)
    uint8_t lib = (config->actuator_type == HAPTIC_ACTUATOR_LRA) ?
                   DRV2605_LIB_LRA : DRV2605_LIB_ERM_A;
    haptic_drv2605_write_reg(DRV2605_REG_LIBRARY, lib);

    // Control registers for closed-loop operation
    haptic_drv2605_write_reg(DRV2605_REG_CONTROL1, 0x93); // Drive time
    haptic_drv2605_write_reg(DRV2605_REG_CONTROL2, 0xF5); // Sample time, blanking
    haptic_drv2605_write_reg(DRV2605_REG_CONTROL3, 0x80); // Auto-resonance, ERM open-loop

    // Set default mode
    haptic_drv2605_set_mode(config->default_mode);

    printf("[Haptic] DRV2605 initialized (actuator=%s)\n",
           config->actuator_type == HAPTIC_ACTUATOR_LRA ? "LRA" : "ERM");

    return true;
}

bool haptic_drv2605_auto_calibrate(void) {
    printf("[Haptic] Running auto-calibration...\n");

    // Set mode to auto-calibration
    haptic_drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_AUTOCAL);

    // Trigger calibration
    haptic_drv2605_write_reg(DRV2605_REG_GO, 0x01);

    // Wait for calibration (can take up to 1 second for LRA)
#ifdef PLATFORM_ESP32
    vTaskDelay(pdMS_TO_TICKS(1200));
#else
    // Desktop: just simulate
#endif

    // Check status
    uint8_t status = 0;
    haptic_drv2605_read_reg(DRV2605_REG_STATUS, &status);

    if (status & 0x08) { // DIAG_RESULT bit
        printf("[Haptic] Auto-calibration failed (status=0x%02X)\n", status);
        return false;
    }

    // Return to internal trigger mode
    haptic_drv2605_set_mode(HAPTIC_MODE_INTERNAL_TRIGGER);

    printf("[Haptic] Auto-calibration complete\n");
    return true;
}

// ============================================================================
// Force Touch Implementation
// ============================================================================

static bool force_touch_init(const haptic_force_config_t *config) {
#ifdef PLATFORM_ESP32
    // Configure ADC for piezo sensor
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    if (adc_oneshot_new_unit(&init_config, &g_haptic.adc_handle) != ESP_OK) {
        printf("[Haptic] Failed to init ADC for piezo\n");
        return false;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    if (adc_oneshot_config_channel(g_haptic.adc_handle, config->piezo_pin,
                                    &chan_config) != ESP_OK) {
        printf("[Haptic] Failed to configure ADC channel\n");
        return false;
    }

    printf("[Haptic] Force touch initialized (pin=%d, threshold=%d)\n",
           config->piezo_pin, config->threshold);
#else
    printf("[Haptic Sim] Force touch simulated\n");
#endif

    return true;
}

bool haptic_force_detected(void) {
    if (!g_haptic.force_touch_enabled) return true; // Pass through if disabled

    g_haptic.last_force_value = read_piezo_adc();

    if (!g_haptic.force_pressed) {
        // Check for press
        if (g_haptic.last_force_value >= g_haptic.force_config.threshold) {
            g_haptic.force_pressed = true;
            g_haptic.force_press_time = lv_tick_get();
            return true;
        }
    } else {
        // Check for release
        if (g_haptic.last_force_value < g_haptic.force_config.release_threshold) {
            g_haptic.force_pressed = false;
        }
        return true;
    }

    return false;
}

uint16_t haptic_get_force_value(void) {
    return g_haptic.last_force_value;
}

void haptic_set_force_threshold(uint16_t threshold) {
    g_haptic.force_config.threshold = threshold;
}

// ============================================================================
// Haptic Playback
// ============================================================================

void haptic_play(haptic_effect_t effect) {
    if (!g_haptic.enabled || !g_haptic.initialized) return;
    if (effect == 0 || effect > HAPTIC_EFFECT_MAX) return;

    printf("[Haptic] Playing effect %d\n", effect);

    // Set to internal trigger mode
    haptic_drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL);

    // Load effect into waveform sequencer slot 1
    haptic_drv2605_write_reg(DRV2605_REG_WAVESEQ1, effect);

    // End sequence
    haptic_drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0);

    // Trigger playback
    haptic_drv2605_write_reg(DRV2605_REG_GO, 0x01);
}

void haptic_play_sequence(const haptic_effect_t *effects, uint8_t count) {
    if (!g_haptic.enabled || !g_haptic.initialized) return;
    if (count == 0 || count > 8) return;

    printf("[Haptic] Playing sequence of %d effects\n", count);

    // Set to internal trigger mode
    haptic_drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTERNAL);

    // Load effects into waveform sequencer
    for (uint8_t i = 0; i < count; i++) {
        haptic_drv2605_write_reg(DRV2605_REG_WAVESEQ1 + i, effects[i]);
    }

    // End sequence
    if (count < 8) {
        haptic_drv2605_write_reg(DRV2605_REG_WAVESEQ1 + count, 0);
    }

    // Trigger playback
    haptic_drv2605_write_reg(DRV2605_REG_GO, 0x01);
}

void haptic_stop(void) {
    haptic_drv2605_write_reg(DRV2605_REG_GO, 0x00);
}

// ============================================================================
// LVGL Widget Event Handlers
// ============================================================================

// User data for haptic-enabled widgets
typedef struct {
    haptic_button_config_t btn_config;
    bool waiting_for_force;
    uint32_t press_start_time;
} haptic_btn_data_t;

typedef struct {
    haptic_slider_config_t slider_config;
    int32_t last_tick_value;
    bool at_end_stop;
} haptic_slider_data_t;

typedef struct {
    haptic_switch_config_t sw_config;
} haptic_switch_data_t;

// Button event handler with force touch support
static void haptic_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    haptic_btn_data_t *data = (haptic_btn_data_t *)lv_event_get_user_data(e);

    if (!data) return;

    switch (code) {
        case LV_EVENT_PRESSED:
            if (data->btn_config.force_touch_required) {
                // Wait for force touch before triggering
                data->waiting_for_force = true;
                data->press_start_time = lv_tick_get();
            } else {
                // Immediate haptic feedback (desktop mode or force touch disabled)
                data->press_start_time = lv_tick_get();
                haptic_play(data->btn_config.press_effect);
            }
            break;

        case LV_EVENT_PRESSING:
            if (data->waiting_for_force && haptic_force_detected()) {
                // Force touch detected! Trigger haptic and allow click to proceed
                data->waiting_for_force = false;
                haptic_play(data->btn_config.press_effect);
                // Click will be handled naturally by LVGL now that we're not waiting anymore
            }
            // Check for long press
            if (data->btn_config.long_press_effect != 0 && data->press_start_time != UINT32_MAX) {
                uint32_t elapsed = lv_tick_elaps(data->press_start_time);
                if (elapsed >= data->btn_config.long_press_time_ms) {
                    haptic_play(data->btn_config.long_press_effect);
                    data->press_start_time = UINT32_MAX; // Prevent retriggering
                }
            }
            break;

        case LV_EVENT_CLICKED:
        case LV_EVENT_SHORT_CLICKED:
        case LV_EVENT_SINGLE_CLICKED:
            // Block click events if force touch is required but not yet detected
            if (data->btn_config.force_touch_required && data->waiting_for_force) {
                printf("[Haptic] Click blocked - waiting for force touch\n");
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
                return; // Prevent further processing
            }
            // Click is allowed - either force touch not required or already detected
            break;

        case LV_EVENT_LONG_PRESSED:
        case LV_EVENT_LONG_PRESSED_REPEAT:
            // Long press events - same logic: require force touch if enabled
            if (data->btn_config.force_touch_required && data->waiting_for_force) {
                printf("[Haptic] Long press blocked - waiting for force touch\n");
                lv_event_stop_bubbling(e);
                lv_event_stop_processing(e);
                return;
            }
            break;

        case LV_EVENT_RELEASED:
            if (data->btn_config.release_effect != 0 && !data->waiting_for_force) {
                haptic_play(data->btn_config.release_effect);
            }
            // Reset waiting state on release
            data->waiting_for_force = data->btn_config.force_touch_required;
            break;

        // Freeing happens in haptic_btn_delete_cb below, not here: this
        // callback is registered twice on the same object (LV_EVENT_PREPROCESS
        // and LV_EVENT_ALL), so LV_EVENT_DELETE would otherwise reach this
        // switch twice with the same `data` pointer and double-free it.
        default:
            break;
    }
}

// Fires exactly once per object (registered without LV_EVENT_PREPROCESS,
// unlike haptic_btn_event_cb above), so it is the single owner of `data`.
static void haptic_btn_delete_cb(lv_event_t *e) {
    haptic_btn_data_t *data = (haptic_btn_data_t *)lv_event_get_user_data(e);
    free(data);
}

// Slider event handler with groove feedback
static void haptic_slider_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    haptic_slider_data_t *data = (haptic_slider_data_t *)lv_event_get_user_data(e);

    if (!data) return;

    switch (code) {
        case LV_EVENT_VALUE_CHANGED: {
            int32_t value = lv_slider_get_value(slider);
            int32_t min_val = lv_slider_get_min_value(slider);
            int32_t max_val = lv_slider_get_max_value(slider);

            // Check for end stops
            if ((value == min_val || value == max_val) && !data->at_end_stop) {
                data->at_end_stop = true;
                haptic_play(data->slider_config.end_stop_effect);
            } else if (value != min_val && value != max_val) {
                data->at_end_stop = false;
            }

            // Check for tick marks (grooves)
            if (data->slider_config.tick_effect != 0) {
                int32_t tick_spacing;

                if (data->slider_config.tick_count > 0) {
                    // Calculate spacing from tick count
                    tick_spacing = (max_val - min_val) / (data->slider_config.tick_count - 1);
                } else {
                    // Use configured spacing
                    tick_spacing = data->slider_config.tick_spacing;
                }

                if (tick_spacing > 0) {
                    // Determine which tick we're at
                    int32_t current_tick = (value - min_val) / tick_spacing;
                    int32_t last_tick = (data->last_tick_value - min_val) / tick_spacing;

                    if (current_tick != last_tick) {
                        haptic_play(data->slider_config.tick_effect);
                    }
                }
            }

            data->last_tick_value = value;
            break;
        }

        case LV_EVENT_DELETE:
            free(data);
            break;

        default:
            break;
    }
}

// Switch event handler
static void haptic_switch_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *sw = lv_event_get_target(e);
    haptic_switch_data_t *data = (haptic_switch_data_t *)lv_event_get_user_data(e);

    if (!data) return;

    switch (code) {
        case LV_EVENT_VALUE_CHANGED: {
            bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
            if (checked) {
                haptic_play(data->sw_config.on_effect);
            } else {
                haptic_play(data->sw_config.off_effect);
            }
            break;
        }

        case LV_EVENT_DELETE:
            free(data);
            break;

        default:
            break;
    }
}

// Simple click handler
static void haptic_click_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    haptic_effect_t effect = (haptic_effect_t)(uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        haptic_play(effect);
    }
}

// Force touch click handler
static void haptic_force_click_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    haptic_effect_t effect = (haptic_effect_t)(uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSING) {
        if (haptic_force_detected()) {
            haptic_play(effect);
            // Prevent multiple triggers by clearing this handler temporarily
        }
    }
}

// ============================================================================
// Public Widget Creation Functions
// ============================================================================

// Default configurations
static const haptic_button_config_t default_btn_config = {
    .force_touch_required = false,
    .press_effect = HAPTIC_BUTTON_PRESS,
    .release_effect = 0,
    .long_press_effect = 0,
    .long_press_time_ms = 500
};

static const haptic_slider_config_t default_slider_config = {
    .force_touch_required = false,
    .tick_effect = HAPTIC_SLIDER_TICK,
    .end_stop_effect = HAPTIC_SLIDER_END_STOP,
    .tick_count = 0,
    .tick_spacing = 10
};

static const haptic_switch_config_t default_switch_config = {
    .on_effect = HAPTIC_TOGGLE_ON,
    .off_effect = HAPTIC_TOGGLE_OFF
};

lv_obj_t *haptic_btn_create(lv_obj_t *parent, const haptic_button_config_t *config) {
    lv_obj_t *btn = lv_btn_create(parent);
    haptic_btn_add(btn, config);
    return btn;
}

void haptic_btn_add(lv_obj_t *btn, const haptic_button_config_t *config) {
    haptic_btn_data_t *data = malloc(sizeof(haptic_btn_data_t));
    if (!data) return;

    if (config) {
        data->btn_config = *config;
    } else {
        data->btn_config = default_btn_config;
    }

    // Disable force touch on desktop platform
#ifndef ESP_PLATFORM
    data->btn_config.force_touch_required = false;
#endif

    data->waiting_for_force = data->btn_config.force_touch_required;
    data->press_start_time = 0;

    // Register event handler with PREPROCESS to catch events before other handlers
    lv_obj_add_event_cb(btn, haptic_btn_event_cb, LV_EVENT_PREPROCESS, data);
    lv_obj_add_event_cb(btn, haptic_btn_event_cb, LV_EVENT_ALL, data);
    // Single-fire cleanup (see comment on haptic_btn_delete_cb).
    lv_obj_add_event_cb(btn, haptic_btn_delete_cb, LV_EVENT_DELETE, data);
}

lv_obj_t *haptic_slider_create(lv_obj_t *parent, const haptic_slider_config_t *config) {
    lv_obj_t *slider = lv_slider_create(parent);
    haptic_slider_add(slider, config);
    return slider;
}

void haptic_slider_add(lv_obj_t *slider, const haptic_slider_config_t *config) {
    haptic_slider_data_t *data = malloc(sizeof(haptic_slider_data_t));
    if (!data) return;

    if (config) {
        data->slider_config = *config;
    } else {
        data->slider_config = default_slider_config;
    }

    // Disable force touch on desktop platform
#ifndef ESP_PLATFORM
    data->slider_config.force_touch_required = false;
#endif

    data->last_tick_value = lv_slider_get_value(slider);
    data->at_end_stop = false;

    lv_obj_add_event_cb(slider, haptic_slider_event_cb, LV_EVENT_ALL, data);
}

lv_obj_t *haptic_switch_create(lv_obj_t *parent, const haptic_switch_config_t *config) {
    lv_obj_t *sw = lv_switch_create(parent);
    haptic_switch_add(sw, config);
    return sw;
}

void haptic_switch_add(lv_obj_t *sw, const haptic_switch_config_t *config) {
    haptic_switch_data_t *data = malloc(sizeof(haptic_switch_data_t));
    if (!data) return;

    if (config) {
        data->sw_config = *config;
    } else {
        data->sw_config = default_switch_config;
    }

    lv_obj_add_event_cb(sw, haptic_switch_event_cb, LV_EVENT_ALL, data);
}

void haptic_add_click(lv_obj_t *obj, haptic_effect_t effect) {
    lv_obj_add_event_cb(obj, haptic_click_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)effect);
}

void haptic_add_force_touch(lv_obj_t *obj, haptic_effect_t effect) {
    lv_obj_add_event_cb(obj, haptic_force_click_event_cb, LV_EVENT_PRESSING,
                        (void *)(uintptr_t)effect);
}

// ============================================================================
// Initialization
// ============================================================================

bool haptic_init(const haptic_drv2605_config_t *drv_config,
                 const haptic_force_config_t *force_config) {
    if (g_haptic.initialized) {
        printf("[Haptic] Already initialized\n");
        return true;
    }

    // Store configuration
    if (drv_config) {
        g_haptic.drv_config = *drv_config;
    } else {
        // Default DRV2605 config
        g_haptic.drv_config.i2c_addr = 0x5A;
        g_haptic.drv_config.i2c_port = 0;
        g_haptic.drv_config.en_pin = 0xFF; // Not used
        g_haptic.drv_config.actuator_type = HAPTIC_ACTUATOR_LRA;
        g_haptic.drv_config.rated_voltage = 0x50;
        g_haptic.drv_config.overdrive_voltage = 0x89;
        g_haptic.drv_config.default_mode = HAPTIC_MODE_INTERNAL_TRIGGER;
    }

    // Initialize DRV2605
    if (!drv2605_init(&g_haptic.drv_config)) {
        printf("[Haptic] Failed to initialize DRV2605\n");
#ifdef PLATFORM_DESKTOP
        // Continue anyway for desktop simulation
        printf("[Haptic] Running in simulation mode\n");
#else
        return false;
#endif
    }

    // Initialize force touch if configured
    if (force_config) {
        g_haptic.force_config = *force_config;
        g_haptic.force_touch_enabled = force_touch_init(force_config);
    } else {
        g_haptic.force_touch_enabled = false;
    }

    g_haptic.initialized = true;
    g_haptic.enabled = true;

    printf("[Haptic] Initialization complete (force_touch=%s)\n",
           g_haptic.force_touch_enabled ? "enabled" : "disabled");

    return true;
}

void haptic_deinit(void) {
    if (!g_haptic.initialized) return;

    haptic_stop();

#ifdef PLATFORM_ESP32
    if (g_haptic.force_touch_enabled) {
        adc_oneshot_del_unit(g_haptic.adc_handle);
    }
#endif

    // Put DRV2605 in standby
    haptic_drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_STANDBY);

    g_haptic.initialized = false;
    g_haptic.enabled = false;

    printf("[Haptic] Deinitialized\n");
}

bool haptic_is_enabled(void) {
    return g_haptic.enabled;
}

void haptic_set_enabled(bool enable) {
    g_haptic.enabled = enable;
    printf("[Haptic] %s\n", enable ? "Enabled" : "Disabled");
}
