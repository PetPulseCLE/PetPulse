/**
 * @file led_manager.h
 * @brief System-wide LED feedback manager.
 *
 * Owns the single RGB status LED (WS2812B on BSP_LED_RGB). All subsystems
 * post events to this manager via its callback wrappers; the manager resolves
 * priority, renders patterns (including sinusoidal breathe) from a dedicated
 * task, and persists user-facing verbosity/brightness to NVS.
 *
 * Layer model:
 *   - OVERRIDE : hard-set steady colour (button long-press ramp, STUCK)
 *   - OVERLAY  : transient one-shot pattern (notifications, gesture acks)
 *   - BASE     : continuous priority-resolved state (charging, BLE adv, ...)
 *
 * The LED task renders whichever layer is active at 50 Hz.
 *
 * All public functions are thread-safe and may be called from any task. Do
 * NOT call from ISR context.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "ws2812b.h"
#include "bms_driver.h"
#include "ble_driver.h"
#include "a121_radar_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

/** Animation tick period. 20 ms (50 Hz) for flicker-free sine breathing. */
#define LED_MANAGER_TICK_MS          20U

/** LED task stack (bytes). */
#define LED_MANAGER_TASK_STACK       3072U

/** LED task priority. Low: below BMS monitor (3), button, and IMU. */
#define LED_MANAGER_TASK_PRIORITY    2U

/** Inbound event queue depth. */
#define LED_MANAGER_QUEUE_DEPTH      8U

/** NVS namespace used for persisted settings. */
#define LED_MANAGER_NVS_NAMESPACE    "led"

/** Default maximum brightness scaling (0-255). */
#define LED_MANAGER_DEFAULT_BRIGHT   255U

/* ========================================================================== */
/*  Verbosity                                                                 */
/* ========================================================================== */

typedef enum {
    LED_VERBOSITY_OFF      = 0, /**< LED completely disabled. */
    LED_VERBOSITY_CRITICAL = 1, /**< Faults, critical battery, boot fail only. */
    LED_VERBOSITY_POWER    = 2, /**< + charging / USB / low battery. */
    LED_VERBOSITY_STATUS   = 3, /**< + BLE connection + boot init results. */
    LED_VERBOSITY_FULL     = 4, /**< + IMU motion, radar presence. */
    LED_VERBOSITY_DEBUG    = 5, /**< + all debug blips (step, MTU, time-sync). */
} led_verbosity_t;

/** Default verbosity used when NVS is empty. */
#define LED_VERBOSITY_DEFAULT        LED_VERBOSITY_FULL

/* ========================================================================== */
/*  Patterns                                                                  */
/* ========================================================================== */

typedef enum {
    LED_PAT_OFF,                  /**< Hard off. */
    LED_PAT_STEADY,               /**< Constant colour. */
    LED_PAT_BLINK,                /**< N on/off cycles at period_ms, then off. */
    LED_PAT_BREATHE,              /**< Sinusoidal fade, period_ms per cycle. */
    LED_PAT_STROBE,               /**< Fast on/off at period_ms, blink_count cycles. */
    LED_PAT_FADE_OUT,             /**< Linear fade from full to off over period_ms. */
    LED_PAT_DOUBLE_BLINK_REPEAT,  /**< Two quick blinks, pause, repeat. */
} led_pattern_t;

/* ========================================================================== */
/*  Semantic base states                                                      */
/* ========================================================================== */

/**
 * Base states — continuous, priority-resolved. Each has an entry in the
 * internal priority table mapping it to a colour, pattern, and verbosity
 * threshold. Higher numeric value == higher priority.
 */
typedef enum {
    LED_STATE_OFF = 0,

    /* Baseline / idle */
    LED_STATE_SYSTEM_READY,
    LED_STATE_BLE_CONNECTED_IDLE,
    LED_STATE_BLE_ADVERTISING,

    /* Radar */
    LED_STATE_RADAR_INTRA,
    LED_STATE_RADAR_DISTANCE,
    LED_STATE_RADAR_ESTIMATING,
    LED_STATE_VITALS_LOCKED,

    /* Boot */
    LED_STATE_BOOT,

    /* Power */
    LED_STATE_USB_CONNECTED,
    LED_STATE_CHARGE_DONE,
    LED_STATE_CHARGING_TOPOFF,
    LED_STATE_CHARGING,
    LED_STATE_LOW_BATTERY,

    /* Critical */
    LED_STATE_CRITICAL_BATTERY,
    LED_STATE_CHARGER_FAULT,
    LED_STATE_OTG_FAULT,
    LED_STATE_BOOT_FAIL,          /**< Latched until reboot. */

    LED_STATE__COUNT,
} led_state_t;

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

/**
 * Initialise the LED manager.
 *
 * - Loads verbosity + brightness from NVS (falls back to defaults).
 * - Creates the event queue and LED task.
 * - Assumes ws2812b_init() has already been called.
 *
 * Safe to call only once. Subsequent calls return ESP_ERR_INVALID_STATE.
 */
esp_err_t led_manager_init(void);

/**
 * Tear down the LED manager. Stops the task, blanks the LED, frees resources.
 */
esp_err_t led_manager_deinit(void);

/** Report whether led_manager_init() has completed successfully. */
bool led_manager_is_ready(void);

/* ========================================================================== */
/*  Verbosity / brightness                                                    */
/* ========================================================================== */

/** Set verbosity. Persists to NVS. May silence the LED immediately. */
void led_manager_set_verbosity(led_verbosity_t v);

led_verbosity_t led_manager_get_verbosity(void);

/** Set max brightness (0-255). Persists to NVS. */
void led_manager_set_max_brightness(uint8_t brightness);

uint8_t led_manager_get_max_brightness(void);

/* ========================================================================== */
/*  Base state control                                                        */
/* ========================================================================== */

/**
 * Request a base state. The manager keeps a bitset of which base states are
 * currently "asserted" by subsystems, and renders whichever is highest
 * priority. Asserting a state that is below the current top is remembered
 * but not rendered.
 */
void led_manager_set_base(led_state_t state);

/**
 * Clear a previously-asserted base state. If this was the active one, the
 * next-highest asserted state is rendered (or OFF if none).
 */
void led_manager_clear_base(led_state_t state);

/** Clear every base-state assertion — used during shutdown. */
void led_manager_clear_all_base(void);

/* ========================================================================== */
/*  One-shot overlay                                                          */
/* ========================================================================== */

/**
 * Play a transient pattern over the top of the current base state. The
 * overlay auto-clears after duration_ms and the base state resumes.
 *
 * Replaces any overlay currently in flight (latest wins).
 *
 * @param color        Colour for the overlay.
 * @param pat          Pattern — STEADY for a simple flash.
 * @param blink_count  Number of blinks/strobes (ignored for steady/breathe).
 * @param duration_ms  Total overlay duration. For STEADY, this is the on-time.
 */
void led_manager_notify(ws2812b_color_t color,
                        led_pattern_t pat,
                        uint32_t blink_count,
                        uint32_t duration_ms);

/** Force-clear any active overlay immediately. */
void led_manager_clear_overlay(void);

/* ========================================================================== */
/*  Hard override (button-owned)                                              */
/* ========================================================================== */

/**
 * Hold the LED at a fixed colour, regardless of base/overlay state.
 *
 * Used by the button driver for:
 *   - Long-press brightness ramp (colour animated externally)
 *   - STUCK indicator (constant red)
 *   - HARD_RESET_IMMINENT (dim red)
 *
 * Remains active until led_manager_override_release() is called.
 */
void led_manager_override_steady(ws2812b_color_t color);

/** Release the override. Base + overlay render normally again. */
void led_manager_override_release(void);

/* ========================================================================== */
/*  Subsystem event hooks                                                     */
/* ========================================================================== */

/** Forward a BMS event. Call from inside the existing power event handler. */
void led_manager_on_power_event(const power_event_t *ev);

/** Forward a BLE event. Call from inside the existing BLE event handler. */
void led_manager_on_ble_event(const ble_event_t *ev);

/**
 * Forward a radar state transition.
 * @param state            Current state reported by a121_vitals_process().
 * @param result_ready     True when a breathing result is available.
 * @param hr_confidence    Heart-rate confidence 0-100 (pass 0 if unknown).
 */
void led_manager_on_radar_state(a121_vitals_state_t state,
                                bool result_ready,
                                float hr_confidence);

/** Forward an IMU "motion detected" event — shows a brief overlay at FULL verbosity. */
void led_manager_on_imu_motion(void);

/* ========================================================================== */
/*  Boot sequence helpers                                                     */
/* ========================================================================== */

/**
 * Announce a subsystem successfully initialised. Plays a short confirmation
 * blink if verbosity >= STATUS. The first call also establishes the BOOT
 * base state.
 */
void led_manager_boot_stage_ok(const char *tag);

/**
 * Announce a subsystem init failure. Plays a critical blink at verbosity >=
 * CRITICAL and latches LED_STATE_BOOT_FAIL as the base state (until reboot).
 */
void led_manager_boot_stage_fail(const char *tag);

/**
 * Announce that all critical boot steps have finished. Clears the BOOT base
 * state and plays a short SYSTEM_READY confirmation (green 1s steady) at
 * verbosity >= STATUS, unless BOOT_FAIL is latched.
 */
void led_manager_boot_complete(void);

#ifdef __cplusplus
}
#endif
