/**
 * @file findmy_keys.h
 * @brief Apple Find My data storage — compile-time defaults + NVS runtime overrides.
 *
 * == Provisioning flow (Cloned AirTag / nRF Connect dump) ==
 *
 * 1. **Development / factory flash**: Capture an AirTag's beacon using nRF Connect.
 *    Paste the MAC Address into FINDMY_DEFAULT_MAC, and the raw payload into
 *    FINDMY_DEFAULT_PAYLOAD below. The firmware ships with this raw dump baked in.
 *
 * 2. **Runtime update (production)**: The companion app calls findmy_data_set()
 *    with a new MAC and payload. This data is persisted in NVS and takes
 *    priority over the compiled-in default on every subsequent boot.
 *
 * 3. **Factory reset**: Call findmy_data_erase() to wipe the NVS data.
 *    The next findmy_data_load() will fall back to the compiled default.
 *
 * == Why NVS? ==
 * - Survives OTA updates.
 * - Wear-levelled flash with CRC validation.
 * - Zero additional overhead (already used by BLE stack).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Length of Find My MAC Address */
#define FINDMY_MAC_LEN          6

/** Length of Find My Advertisement Payload */
#define FINDMY_PAYLOAD_LEN     31

/**
 * Default MAC Address (Hex-string, can include colons or dashes).
 *
 * Paste the MAC address of the clone here from your nRF Connect dump.
 * e.g., "12:34:56:78:9A:BC" or "12-34-56-78-9A-BC".
 */
#define FINDMY_DEFAULT_MAC "C4:DE:BB:84:D3:A7"

/**
 * Default Payload (Hex-string).
 *
 * Paste the exact hex data from the nRF Connect dump (from the 0x1E byte).
 * e.g., "1EFF4C001219..."
 */
#define FINDMY_DEFAULT_PAYLOAD "1EFF4C00121910809B873C11E8532740A397B3FA95D1B7CBF95A1476AD0318"

/**
 * Load the active Find My data (MAC and Payload).
 *
 * Priority:
 *   1. NVS data (if previously set via findmy_data_set)
 *   2. Compiled-in default (FINDMY_DEFAULT_MAC / FINDMY_DEFAULT_PAYLOAD)
 *
 * @param[out] mac_out      6-byte buffer for the MAC address
 * @param[out] payload_out  31-byte buffer for the advertising payload
 * @return  true   Valid data was loaded
 * @return  false  No data available (both NVS and default are empty/zero)
 */
bool findmy_data_load(uint8_t mac_out[FINDMY_MAC_LEN], uint8_t payload_out[FINDMY_PAYLOAD_LEN]);

/**
 * Store new Find My data in NVS (persists across reboots and OTA updates).
 *
 * @param mac      6-byte MAC Address
 * @param payload  31-byte raw payload
 * @return ESP_OK on success
 */
esp_err_t findmy_data_set(const uint8_t mac[FINDMY_MAC_LEN], const uint8_t payload[FINDMY_PAYLOAD_LEN]);

/**
 * Erase the NVS data (factory reset).
 * Next boot will use the compiled-in default.
 *
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no data was stored
 */
esp_err_t findmy_data_erase(void);

#ifdef __cplusplus
}
#endif
