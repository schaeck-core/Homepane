/**
 * @file haptic.h
 * @brief Haptic Feedback API with Force Touch Detection
 * 
 * This module provides:
 * - Force touch detection via piezo buzzers
 * - Haptic feedback via DRV2605 driver
 * - LVGL widget event wrappers for enhanced touch experience
 * 
 * The DRV2605 supports 123 haptic effects in its ROM library.
 * Common effects used:
 * - Strong Click: Effect 1
 * - Sharp Click: Effect 4  
 * - Soft Bump: Effect 7
 * - Double Click: Effect 10
 * - Pulsing Strong: Effect 14
 * - Buzz: Effect 47-52
 * - Smooth Hum: Effect 64-69
 * - Ramp Up/Down: Effect 70-81
 * - Transition: Effect 82-89
 */

#ifndef HAPTIC_H
#define HAPTIC_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DRV2605 Effect Library (ROM Effects 1-123)
// ============================================================================

typedef enum {
    // Strong Clicks
    HAPTIC_EFFECT_STRONG_CLICK_100      = 1,
    HAPTIC_EFFECT_STRONG_CLICK_60       = 2,
    HAPTIC_EFFECT_STRONG_CLICK_30       = 3,
    HAPTIC_EFFECT_SHARP_CLICK_100       = 4,
    HAPTIC_EFFECT_SHARP_CLICK_60        = 5,
    HAPTIC_EFFECT_SHARP_CLICK_30        = 6,
    
    // Soft Bumps
    HAPTIC_EFFECT_SOFT_BUMP_100         = 7,
    HAPTIC_EFFECT_SOFT_BUMP_60          = 8,
    HAPTIC_EFFECT_SOFT_BUMP_30          = 9,
    
    // Double Clicks
    HAPTIC_EFFECT_DOUBLE_CLICK_100      = 10,
    HAPTIC_EFFECT_DOUBLE_CLICK_60       = 11,
    
    // Triple Click
    HAPTIC_EFFECT_TRIPLE_CLICK_100      = 12,
    
    // Soft Fuzz
    HAPTIC_EFFECT_SOFT_FUZZ_60          = 13,
    
    // Pulsing
    HAPTIC_EFFECT_PULSING_STRONG_1      = 14,
    HAPTIC_EFFECT_PULSING_STRONG_2      = 15,
    HAPTIC_EFFECT_PULSING_MEDIUM_1      = 16,
    HAPTIC_EFFECT_PULSING_MEDIUM_2      = 17,
    HAPTIC_EFFECT_PULSING_SHARP_1       = 18,
    HAPTIC_EFFECT_PULSING_SHARP_2       = 19,
    
    // Transition Clicks
    HAPTIC_EFFECT_TRANSITION_CLICK_1    = 20,
    HAPTIC_EFFECT_TRANSITION_CLICK_2    = 21,
    HAPTIC_EFFECT_TRANSITION_CLICK_3    = 22,
    HAPTIC_EFFECT_TRANSITION_CLICK_4    = 23,
    HAPTIC_EFFECT_TRANSITION_CLICK_5    = 24,
    HAPTIC_EFFECT_TRANSITION_CLICK_6    = 25,
    HAPTIC_EFFECT_TRANSITION_CLICK_7    = 26,
    HAPTIC_EFFECT_TRANSITION_CLICK_8    = 27,
    HAPTIC_EFFECT_TRANSITION_CLICK_9    = 28,
    HAPTIC_EFFECT_TRANSITION_CLICK_10   = 29,
    
    // Transition Hums
    HAPTIC_EFFECT_TRANSITION_HUM_1      = 30,
    HAPTIC_EFFECT_TRANSITION_HUM_2      = 31,
    HAPTIC_EFFECT_TRANSITION_HUM_3      = 32,
    HAPTIC_EFFECT_TRANSITION_HUM_4      = 33,
    HAPTIC_EFFECT_TRANSITION_HUM_5      = 34,
    HAPTIC_EFFECT_TRANSITION_HUM_6      = 35,
    HAPTIC_EFFECT_TRANSITION_HUM_7      = 36,
    HAPTIC_EFFECT_TRANSITION_HUM_8      = 37,
    HAPTIC_EFFECT_TRANSITION_HUM_9      = 38,
    HAPTIC_EFFECT_TRANSITION_HUM_10     = 39,
    
    // Transition Ramp Down
    HAPTIC_EFFECT_RAMP_DOWN_LONG_1      = 40,
    HAPTIC_EFFECT_RAMP_DOWN_LONG_2      = 41,
    HAPTIC_EFFECT_RAMP_DOWN_MEDIUM_1    = 42,
    HAPTIC_EFFECT_RAMP_DOWN_MEDIUM_2    = 43,
    HAPTIC_EFFECT_RAMP_DOWN_SHORT_1     = 44,
    HAPTIC_EFFECT_RAMP_DOWN_SHORT_2     = 45,
    
    // Transition Ramp Up
    HAPTIC_EFFECT_RAMP_UP_LONG_1        = 46,
    HAPTIC_EFFECT_RAMP_UP_LONG_2        = 47,
    HAPTIC_EFFECT_RAMP_UP_MEDIUM_1      = 48,
    HAPTIC_EFFECT_RAMP_UP_MEDIUM_2      = 49,
    HAPTIC_EFFECT_RAMP_UP_SHORT_1       = 50,
    HAPTIC_EFFECT_RAMP_UP_SHORT_2       = 51,
    
    // Buzz
    HAPTIC_EFFECT_BUZZ_1                = 52,
    HAPTIC_EFFECT_BUZZ_2                = 53,
    HAPTIC_EFFECT_BUZZ_3                = 54,
    HAPTIC_EFFECT_BUZZ_4                = 55,
    HAPTIC_EFFECT_BUZZ_5                = 56,
    
    // Alert
    HAPTIC_EFFECT_ALERT_750MS           = 57,
    HAPTIC_EFFECT_ALERT_1000MS          = 58,
    
    // Strong Buzz
    HAPTIC_EFFECT_STRONG_BUZZ_100       = 59,
    HAPTIC_EFFECT_STRONG_BUZZ_80        = 60,
    HAPTIC_EFFECT_STRONG_BUZZ_60        = 61,
    HAPTIC_EFFECT_STRONG_BUZZ_40        = 62,
    HAPTIC_EFFECT_STRONG_BUZZ_20        = 63,
    
    // Smooth Hum
    HAPTIC_EFFECT_SMOOTH_HUM_1          = 64,
    HAPTIC_EFFECT_SMOOTH_HUM_2          = 65,
    HAPTIC_EFFECT_SMOOTH_HUM_3          = 66,
    HAPTIC_EFFECT_SMOOTH_HUM_4          = 67,
    HAPTIC_EFFECT_SMOOTH_HUM_5          = 68,
    
    // Sharp Tick
    HAPTIC_EFFECT_SHARP_TICK_1          = 69,
    HAPTIC_EFFECT_SHARP_TICK_2          = 70,
    HAPTIC_EFFECT_SHARP_TICK_3          = 71,
    
    // Short Double Click Strong
    HAPTIC_EFFECT_SHORT_DOUBLE_STRONG_1 = 72,
    HAPTIC_EFFECT_SHORT_DOUBLE_STRONG_2 = 73,
    HAPTIC_EFFECT_SHORT_DOUBLE_STRONG_3 = 74,
    HAPTIC_EFFECT_SHORT_DOUBLE_STRONG_4 = 75,
    
    // Short Double Click Medium
    HAPTIC_EFFECT_SHORT_DOUBLE_MEDIUM_1 = 76,
    HAPTIC_EFFECT_SHORT_DOUBLE_MEDIUM_2 = 77,
    HAPTIC_EFFECT_SHORT_DOUBLE_MEDIUM_3 = 78,
    
    // Short Double Sharp
    HAPTIC_EFFECT_SHORT_DOUBLE_SHARP_1  = 79,
    HAPTIC_EFFECT_SHORT_DOUBLE_SHARP_2  = 80,
    HAPTIC_EFFECT_SHORT_DOUBLE_SHARP_3  = 81,
    
    // Long Double Sharp
    HAPTIC_EFFECT_LONG_DOUBLE_SHARP_1   = 82,
    HAPTIC_EFFECT_LONG_DOUBLE_SHARP_2   = 83,
    HAPTIC_EFFECT_LONG_DOUBLE_SHARP_3   = 84,
    HAPTIC_EFFECT_LONG_DOUBLE_SHARP_4   = 85,
    
    // Tick
    HAPTIC_EFFECT_TICK                  = 86,
    
    // Click
    HAPTIC_EFFECT_CLICK                 = 87,
    
    // Short Click
    HAPTIC_EFFECT_SHORT_CLICK           = 88,
    
    // Bump
    HAPTIC_EFFECT_BUMP                  = 89,
    
    // Max effect number
    HAPTIC_EFFECT_MAX                   = 123
} haptic_effect_t;

// ============================================================================
// Semantic Effect Aliases (for common use cases)
// ============================================================================

// Button interactions
#define HAPTIC_BUTTON_PRESS         HAPTIC_EFFECT_STRONG_CLICK_100
#define HAPTIC_BUTTON_RELEASE       HAPTIC_EFFECT_SOFT_BUMP_30
#define HAPTIC_BUTTON_LONG_PRESS    HAPTIC_EFFECT_DOUBLE_CLICK_100

// Slider interactions
#define HAPTIC_SLIDER_TICK          HAPTIC_EFFECT_SHARP_TICK_1
#define HAPTIC_SLIDER_END_STOP      HAPTIC_EFFECT_STRONG_CLICK_60
#define HAPTIC_SLIDER_DETENT        HAPTIC_EFFECT_SOFT_BUMP_60

// Switch/Toggle
#define HAPTIC_TOGGLE_ON            HAPTIC_EFFECT_SHARP_CLICK_100
#define HAPTIC_TOGGLE_OFF           HAPTIC_EFFECT_SHARP_CLICK_60

// Scroll/List
#define HAPTIC_SCROLL_TICK          HAPTIC_EFFECT_SHARP_TICK_3
#define HAPTIC_SCROLL_BOUNDARY      HAPTIC_EFFECT_STRONG_BUZZ_40

// Notifications
#define HAPTIC_NOTIFICATION         HAPTIC_EFFECT_ALERT_750MS
#define HAPTIC_WARNING              HAPTIC_EFFECT_STRONG_BUZZ_100
#define HAPTIC_SUCCESS              HAPTIC_EFFECT_TRIPLE_CLICK_100
#define HAPTIC_ERROR                HAPTIC_EFFECT_ALERT_1000MS

// ============================================================================
// Force Touch Configuration
// ============================================================================

typedef struct {
    uint16_t threshold;           // ADC threshold for force detection (0-4095)
    uint16_t release_threshold;   // ADC threshold for release detection
    uint8_t debounce_ms;          // Debounce time in milliseconds
    uint8_t piezo_pin;            // GPIO pin for piezo sensor (ADC capable)
} haptic_force_config_t;

// ============================================================================
// DRV2605 Configuration
// ============================================================================

typedef enum {
    HAPTIC_MODE_INTERNAL_TRIGGER = 0,  // Trigger by writing to GO bit
    HAPTIC_MODE_EXT_EDGE_TRIGGER = 1,  // Trigger on rising edge of IN/TRIG
    HAPTIC_MODE_EXT_LEVEL_TRIGGER = 2, // Trigger while IN/TRIG is high
    HAPTIC_MODE_PWM_ANALOG = 3,        // PWM/Analog input for real-time control
    HAPTIC_MODE_AUDIO_TO_VIBE = 4,     // Audio-to-vibe (AC coupling)
    HAPTIC_MODE_RTP = 5,               // Real-time playback
    HAPTIC_MODE_DIAGNOSTICS = 6,       // Diagnostics
    HAPTIC_MODE_AUTO_CALIBRATION = 7   // Auto calibration
} haptic_mode_t;

typedef enum {
    HAPTIC_ACTUATOR_ERM = 0,           // Eccentric Rotating Mass
    HAPTIC_ACTUATOR_LRA = 1            // Linear Resonant Actuator
} haptic_actuator_t;

typedef struct {
    uint8_t i2c_addr;                  // I2C address (default 0x5A)
    uint8_t i2c_port;                  // I2C port number
    uint8_t en_pin;                    // Enable pin (optional, -1 if not used)
    haptic_actuator_t actuator_type;   // ERM or LRA
    uint8_t rated_voltage;             // Rated voltage register value
    uint8_t overdrive_voltage;         // Overdrive clamp register value
    haptic_mode_t default_mode;        // Default operating mode
} haptic_drv2605_config_t;

// ============================================================================
// LVGL Widget Haptic Configuration
// ============================================================================

typedef struct {
    bool force_touch_required;         // Require force touch to trigger
    haptic_effect_t press_effect;      // Effect on press/click
    haptic_effect_t release_effect;    // Effect on release (0 = none)
    haptic_effect_t long_press_effect; // Effect on long press (0 = none)
    uint16_t long_press_time_ms;       // Time for long press detection
} haptic_button_config_t;

typedef struct {
    bool force_touch_required;         // Require force touch for slider
    haptic_effect_t tick_effect;       // Effect for groove/tick marks
    haptic_effect_t end_stop_effect;   // Effect at min/max bounds
    uint8_t tick_count;                // Number of ticks (0 = continuous)
    int32_t tick_spacing;              // Value spacing between ticks (if tick_count=0)
} haptic_slider_config_t;

typedef struct {
    haptic_effect_t on_effect;         // Effect when toggling ON
    haptic_effect_t off_effect;        // Effect when toggling OFF
} haptic_switch_config_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Initialize the haptic feedback system
 * @param drv_config DRV2605 driver configuration
 * @param force_config Force touch sensor configuration (NULL to disable)
 * @return true on success
 */
bool haptic_init(const haptic_drv2605_config_t *drv_config, 
                 const haptic_force_config_t *force_config);

/**
 * @brief Deinitialize the haptic feedback system
 */
void haptic_deinit(void);

/**
 * @brief Check if haptic feedback is enabled
 * @return true if enabled
 */
bool haptic_is_enabled(void);

/**
 * @brief Enable or disable haptic feedback globally
 * @param enable true to enable
 */
void haptic_set_enabled(bool enable);

/**
 * @brief Play a haptic effect
 * @param effect Effect number from haptic_effect_t
 */
void haptic_play(haptic_effect_t effect);

/**
 * @brief Play a sequence of haptic effects
 * @param effects Array of effect numbers
 * @param count Number of effects in array (max 8)
 */
void haptic_play_sequence(const haptic_effect_t *effects, uint8_t count);

/**
 * @brief Stop any playing haptic effect
 */
void haptic_stop(void);

/**
 * @brief Check if force touch is currently detected
 * @return true if force is detected above threshold
 */
bool haptic_force_detected(void);

/**
 * @brief Get current force sensor reading
 * @return ADC value (0-4095)
 */
uint16_t haptic_get_force_value(void);

/**
 * @brief Set force touch threshold
 * @param threshold ADC threshold value
 */
void haptic_set_force_threshold(uint16_t threshold);

// ============================================================================
// LVGL Widget Wrappers
// ============================================================================

/**
 * @brief Create a haptic-enabled button
 * @param parent Parent object
 * @param config Button haptic configuration (NULL for defaults)
 * @return Created button object
 */
lv_obj_t *haptic_btn_create(lv_obj_t *parent, const haptic_button_config_t *config);

/**
 * @brief Add haptic feedback to existing button
 * @param btn Button object
 * @param config Button haptic configuration (NULL for defaults)
 */
void haptic_btn_add(lv_obj_t *btn, const haptic_button_config_t *config);

/**
 * @brief Create a haptic-enabled slider with groove feedback
 * @param parent Parent object
 * @param config Slider haptic configuration (NULL for defaults)
 * @return Created slider object
 */
lv_obj_t *haptic_slider_create(lv_obj_t *parent, const haptic_slider_config_t *config);

/**
 * @brief Add haptic feedback to existing slider
 * @param slider Slider object
 * @param config Slider haptic configuration (NULL for defaults)
 */
void haptic_slider_add(lv_obj_t *slider, const haptic_slider_config_t *config);

/**
 * @brief Create a haptic-enabled switch
 * @param parent Parent object
 * @param config Switch haptic configuration (NULL for defaults)
 * @return Created switch object
 */
lv_obj_t *haptic_switch_create(lv_obj_t *parent, const haptic_switch_config_t *config);

/**
 * @brief Add haptic feedback to existing switch
 * @param sw Switch object
 * @param config Switch haptic configuration (NULL for defaults)
 */
void haptic_switch_add(lv_obj_t *sw, const haptic_switch_config_t *config);

/**
 * @brief Add generic click haptic to any clickable widget
 * @param obj Widget object
 * @param effect Effect to play on click
 */
void haptic_add_click(lv_obj_t *obj, haptic_effect_t effect);

/**
 * @brief Add force-touch requirement to widget
 * @param obj Widget object
 * @param effect Effect to play when force touch triggers
 */
void haptic_add_force_touch(lv_obj_t *obj, haptic_effect_t effect);

// ============================================================================
// Low-level DRV2605 Access (for advanced use)
// ============================================================================

/**
 * @brief Write to DRV2605 register
 * @param reg Register address
 * @param value Value to write
 * @return true on success
 */
bool haptic_drv2605_write_reg(uint8_t reg, uint8_t value);

/**
 * @brief Read from DRV2605 register
 * @param reg Register address
 * @param value Pointer to store read value
 * @return true on success
 */
bool haptic_drv2605_read_reg(uint8_t reg, uint8_t *value);

/**
 * @brief Set DRV2605 operating mode
 * @param mode Operating mode
 */
void haptic_drv2605_set_mode(haptic_mode_t mode);

/**
 * @brief Run DRV2605 auto-calibration
 * @return true on success
 */
bool haptic_drv2605_auto_calibrate(void);

#ifdef __cplusplus
}
#endif

#endif // HAPTIC_H
