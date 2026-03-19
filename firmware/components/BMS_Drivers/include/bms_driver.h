#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Battery Config
#define BATTERY_DESIGN_CAP_MAH         2650
#define BATTERY_V_EMPTY_LSB            0xA561
#define BATTERY_I_CHG_TERM_MA          250
#define BATTERY_RSENSE_OHMS            0.010f

#define BATTERY_SHUTDOWN_VOLTAGE_V     3.0f
#define USB_PRESENT_VOLTAGE_V          4.0f

// Charger Config
#define CHARGER_MAX_CURRENT_MA         1400
#define CHARGER_MAX_VOLTAGE_MV         4200    // Standard Li-ion 4.2V (truncates to 4192mV in 16mV steps)

// Constants
#define BMS_MUTEX_TIMEOUT_MS           100
#define BMS_INIT_RETRY_COUNT           2
#define BMS_INIT_RETRY_DELAY_MS        100

// Enum for selecting which IC to query (Charger or Fuel Gauge)
typedef enum {
    POWER_IC_CHARGER,
    POWER_IC_FUEL_GAUGE
} power_ic_type_t;

// Structure for callback based info retrieval (replicating PropertyValueCallback concept)
typedef void (*power_info_callback_t)(const char* key, const char* value, void* context);

/**
 * Initialize Power Management HAL
 * Sets up I2C, initializes ICs, and starts monitoring tasks.
 */
esp_err_t power_manager_init(void);

/**
 * Check if the Fuel Gauge is healthy and communicating.
 */
bool power_manager_gauge_is_ok(void);

/**
 * Check if a system shutdown is requested by the hardware.
 */
bool power_manager_is_shutdown_requested(void);

/**
 * Get current insomnia level.
 * 0 means sleep is allowed. >0 means sleep is prevented.
 */
uint16_t power_manager_insomnia_level(void);

/**
 * Enter insomnia mode. Prevents the system from sleeping.
 */
void power_manager_insomnia_enter(void);

/**
 * Exit insomnia mode. Allows system sleep if level drops to 0.
 */
void power_manager_insomnia_exit(void);

/**
 * Check if sleep is currently available (insomnia level == 0).
 */
bool power_manager_sleep_available(void);

/**
 * Enter Light Sleep.
 * Wakes up on configured interrupts.
 */
void power_manager_sleep(void);

/**
 * Perform a full system shutdown / deep sleep.
 * System will require a reset or wakeup event to restart.
 */
void power_manager_shutdown(void);

/**
 * Power off the system.
 * Sends poweroff command to charger and prepares RTC.
 */
void power_manager_off(void);

/**
 * Reset the system (Software Reset).
 */
void power_manager_reset(void);

/**
 * Get Battery State of Charge in Percent (0-100%).
 */
uint8_t power_manager_get_pct(void);

/**
 * Get Battery Health in Percent.
 */
uint8_t power_manager_get_bat_health_pct(void);

/**
 * Check if the system is currently charging.
 */
bool power_manager_is_charging(void);

/**
 * Check if charging is complete.
 */
bool power_manager_is_charging_done(void);

/**
 * Enable USB On-The-Go (Boost Mode).
 */
bool power_manager_enable_otg(void);

/**
 * Disable USB On-The-Go (Boost Mode).
 */
void power_manager_disable_otg(void);

/**
 * Check if OTG is currently enabled.
 */
bool power_manager_is_otg_enabled(void);

/**
 * Check and handle OTG faults.
 */
bool power_manager_check_otg_fault(void);

/**
 * Check OTG status and disable if faulted.
 */
void power_manager_check_otg_status(void);

// --- Detailed Data Access ---

/**
 * Get the charging voltage limit (VREG) in Volts.
 */
float power_manager_get_battery_charge_voltage_limit(void);

/**
 * Set the charging voltage limit (VREG) in Volts.
 */
void power_manager_set_battery_charge_voltage_limit(float voltage);

/**
 * Get battery remaining capacity in mAh.
 */
uint32_t power_manager_get_battery_remaining_capacity(void);

/**
 * Get battery full charge capacity in mAh.
 */
uint32_t power_manager_get_battery_full_capacity(void);

/**
 * Get battery design capacity in mAh.
 */
uint32_t power_manager_get_battery_design_capacity(void);

/**
 * Get Battery Voltage in Volts from specific IC.
 */
float power_manager_get_battery_voltage(power_ic_type_t ic);

/**
 * Get Battery Current in Amps from specific IC.
 */
float power_manager_get_battery_current(power_ic_type_t ic);

/**
 * Get Battery Temperature in Degrees Celsius from specific IC.
 */
float power_manager_get_battery_temperature(power_ic_type_t ic);

/**
 * Get USB (VBUS) Voltage in Volts.
 */
float power_manager_get_usb_voltage(void);

// --- Charge Suppression ---

/**
 * Suppress charging.
 * Increments a counter. Charging disabled while counter > 0.
 */
void power_manager_suppress_charge_enter(void);

/**
 * Allow charging.
 * Decrements counter. Charging enabled if counter == 0.
 */
void power_manager_suppress_charge_exit(void);

// --- Info / Debug ---

/**
 * Dump formatted power information via callback.
 * Useful for CLI or GUI display.
 */
void power_manager_info_get(power_info_callback_t out, void* context);

/**
 * Dump detailed debug information via callback.
 */
void power_manager_debug_get(power_info_callback_t out, void* context);

// ============================================================================
// Event-Driven Monitoring
// ============================================================================

/** BMS event types fired by the monitor task */
typedef enum {
    POWER_EVENT_USB_CONNECTED,          ///< USB/VBUS source detected
    POWER_EVENT_USB_DISCONNECTED,       ///< USB/VBUS removed
    POWER_EVENT_CHARGE_START,           ///< Charging started (pre-charge or fast-charge)
    POWER_EVENT_CHARGE_DONE,            ///< Charge complete (still connected)
    POWER_EVENT_CHARGE_STOPPED,         ///< Charging stopped (USB removed or fault)
    POWER_EVENT_LOW_BATTERY,            ///< SOC fell below BMS_LOW_BATTERY_PCT
    POWER_EVENT_CRITICAL_BATTERY,       ///< SOC fell below BMS_CRITICAL_BATTERY_PCT
    POWER_EVENT_CHARGER_FAULT,          ///< BQ25896 charge/NTC/watchdog fault
    POWER_EVENT_OTG_FAULT,              ///< BQ25896 boost/OTG fault
    POWER_EVENT_SNAPSHOT_UPDATED,       ///< Periodic gauge data refreshed
} power_event_type_t;

/** Detailed event payload delivered to the callback */
typedef struct {
    power_event_type_t type;
    union {
        struct {
            uint8_t chrg_stat;          ///< ChrgStat enum value
            uint8_t vbus_stat;          ///< VBusStat enum value
        } status;
        struct {
            uint8_t chrg_fault;         ///< ChrgFault enum value
            uint8_t ntc_fault;          ///< NtcFault enum value
            bool bat_fault;
            bool boost_fault;
            bool watchdog_fault;
        } fault;
        struct {
            uint8_t  soc_pct;
            uint16_t voltage_mv;
        } battery;
    };
} power_event_t;

/** Callback for power events (invoked from monitor task context, NOT ISR) */
typedef void (*power_event_cb_t)(const power_event_t *event, void *user_data);

/** Cached power-state snapshot refreshed by the monitor task */
typedef struct {
    uint8_t  soc_pct;           ///< State of charge (0-100 %)
    uint16_t voltage_mv;        ///< Battery voltage from gauge (mV)
    int32_t  current_ua;        ///< Instantaneous current (uA, signed)
    int32_t  avg_current_ua;    ///< Average current (uA, signed)
    float    temperature_c;     ///< Die temperature (deg C)
    uint16_t tte_min;           ///< Time to empty (minutes, 0xFFFF = unknown)
    uint16_t ttf_min;           ///< Time to full  (minutes, 0xFFFF = unknown)
    bool     usb_connected;     ///< VBUS present
    uint8_t  charge_status;     ///< ChrgStat enum value
    bool     otg_enabled;       ///< OTG boost active
    uint32_t update_tick;       ///< xTaskGetTickCount() at last refresh
} power_snapshot_t;

// Monitor task tunables
#define BMS_MONITOR_TASK_STACK      3072
#define BMS_MONITOR_TASK_PRIORITY   3
#define BMS_GAUGE_POLL_INTERVAL_S   30      ///< MAX17260 poll period (no ALRT pin)
#define BMS_LOW_BATTERY_PCT         15      ///< SOC threshold for LOW_BATTERY event
#define BMS_CRITICAL_BATTERY_PCT    5       ///< SOC threshold for CRITICAL_BATTERY event

/**
 * Start the BMS monitor task.
 * Call after power_manager_init().
 *
 * Installs a GPIO ISR on BSP_INT_BMS (BQ25896 /INT, active-low open-drain)
 * and launches a FreeRTOS task that:
 *   - Wakes immediately on BQ25896 interrupt (charger status/fault changes)
 *   - Polls MAX17260 gauge every BMS_GAUGE_POLL_INTERVAL_S seconds
 *   - Fires the event callback on state transitions and threshold crossings
 *
 * @param callback  Event callback (NULL to disable callbacks but keep polling)
 * @param user_data Opaque pointer forwarded to every callback invocation
 * @return ESP_OK on success
 */
esp_err_t power_manager_monitor_start(power_event_cb_t callback, void *user_data);

/**
 * Stop the BMS monitor task and remove the BQ25896 ISR.
 */
void power_manager_monitor_stop(void);

/**
 * Get a copy of the latest power snapshot (thread-safe).
 * @param[out] snap Destination for snapshot copy
 * @return true if at least one update has occurred, false if not yet available
 */
bool power_manager_get_snapshot(power_snapshot_t *snap);

#ifdef __cplusplus
}
#endif