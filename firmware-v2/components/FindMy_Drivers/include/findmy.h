/**
 * @file findmy.h
 * @brief Apple Find My Network beacon for ESP32-S3 (NimBLE).
 *
 * Broadcasts Apple Offline Finding advertisements so the device appears
 * on the owner's Find My network.  Uses time-division multiplexing with
 * the main PetPulse GATT advertising on the single legacy advertising set:
 *
 *   ┌─────────────────────┐     ┌──────────────┐     ┌───────────────────┐
 *   │ PetPulse connectable│────►│ FindMy burst │────►│ PetPulse resume   │
 *   │  (interval_s - 2s)  │     │  (2 seconds) │     │  (interval_s - 2s)│
 *   └─────────────────────┘     └──────────────┘     └───────────────────┘
 *                                  ▲                        │
 *                                  └────────────────────────┘
 *
 * When a phone is **connected**, PetPulse has no need to advertise, so
 * FindMy runs continuously on the freed advertising slot — this is the
 * most important case (phone connected = pet at home, but Apple's network
 * still builds a location history; phone disconnected = pet lost, FindMy
 * is essential for crowd-sourced location).
 *
 * == Why time-division instead of Extended Advertising? ==
 *
 * NimBLE's extended advertising (CONFIG_BT_NIMBLE_EXT_ADV) replaces the
 * legacy ble_gap_adv_*() API entirely — enabling it would require rewriting
 * the existing BLE driver.  Time-division keeps both modules independent:
 * the BLE driver owns PetPulse, FindMy owns its own bursts, and they
 * coordinate through the public ble_driver API.
 *
 * == Battery status ==
 *
 * The Apple Find My protocol includes a 2-bit battery level in the beacon
 * payload, auto-updated from the BMS snapshot before each advertising burst:
 *   0x00 = Full (>80%), 0x50 = Medium (>50%), 0xA0 = Low (>20%), 0xF0 = Critical
 *
 * == Key management ==
 *
 * See findmy_keys.h for the two-tier key provisioning strategy
 * (compile-time default + NVS runtime override).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ble_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** FindMy beacon interleave period in seconds (1-10).  Default 5s. */
#define FINDMY_DEFAULT_INTERVAL_S   5

/** Duration of each FindMy advertising burst in milliseconds */
#define FINDMY_BURST_DURATION_MS    2000

/**
 * Initialize and start the Apple Find My beacon.
 *
 * Loads the public key from NVS / compiled default (via findmy_keys_load),
 * builds the Apple Offline Finding advertisement payload, and starts
 * the interleave timer.
 *
 * Must be called AFTER ble_driver_init() (NimBLE must be running).
 *
 * @return ESP_OK              Beacon started
 * @return ESP_ERR_NOT_FOUND   No valid public key available
 * @return ESP_FAIL            Resource allocation error
 */
esp_err_t findmy_init(void);

/**
 * Stop the FindMy beacon and release resources.
 */
void findmy_deinit(void);

/**
 * Check if the FindMy beacon is currently active.
 */
bool findmy_is_active(void);

/**
 * Start broadcasting (if stopped).
 * @return ESP_OK or ESP_ERR_INVALID_STATE if no key loaded
 */
esp_err_t findmy_start(void);

/**
 * Stop broadcasting (can be restarted with findmy_start).
 */
void findmy_stop(void);

/**
 * Update the advertising interleave period.
 * @param interval_s  Period in seconds (1-10), clamped to range
 */
void findmy_set_interval(uint8_t interval_s);

/**
 * Reload the data from NVS and rebuild the payload.
 * Call after findmy_data_set() to pick up a new set without rebooting.
 *
 * @return ESP_OK or ESP_ERR_NOT_FOUND if no valid data
 */
esp_err_t findmy_reload_data(void);

/**
 * Update the battery level in the FindMy payload.
 * Called automatically before each burst, but can also be called manually.
 *
 * @param soc_pct  State of charge 0-100%
 */
void findmy_update_battery(uint8_t soc_pct);

/**
 * Notify FindMy of BLE connection/disconnection events.
 *
 * Call this from the BLE event callback in app_main.  FindMy uses
 * connect/disconnect events to switch between interleaved mode
 * (when disconnected) and continuous mode (when connected).
 *
 * @param event  BLE event from the BLE driver callback
 */
void findmy_on_ble_event(const ble_event_t *event);

#ifdef __cplusplus
}
#endif
