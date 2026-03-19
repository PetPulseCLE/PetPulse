/**
 * @file findmy_keys.h
 * @brief Apple Find My key storage — compile-time defaults + NVS runtime overrides.
 *
 * == Key provisioning flow ==
 *
 * 1. **Development / factory flash**: Generate a key pair with generate_keys.py,
 *    paste the 28-byte public key (Base64) into FINDMY_DEFAULT_PUBLIC_KEY_B64
 *    below, and build.  The firmware ships with that key baked in.
 *
 * 2. **Runtime update (production)**: The companion app (or a BLE config
 *    characteristic, or a UART console command) calls findmy_keys_set() with
 *    a new 28-byte public key.  That key is persisted in NVS and takes
 *    priority over the compiled-in default on every subsequent boot.
 *
 * 3. **Factory reset**: Call findmy_keys_erase() to wipe the NVS key.
 *    The next findmy_keys_load() will fall back to the compiled default.
 *
 * == Why NVS for the key file? ==
 *
 * - NVS survives OTA updates — the user doesn't lose their key when firmware
 *   is updated.
 * - NVS is wear-levelled flash with CRC validation — far more reliable than
 *   a raw SPIFFS/LittleFS file for a single 28-byte blob.
 * - NVS is already initialized by the BLE stack (NimBLE bonding), so there's
 *   zero additional overhead.
 * - A key is written once and read on every boot — perfect NVS access pattern.
 * - No filesystem mount needed, no SD card dependency, no file-open/close.
 *
 * == Security note ==
 *
 * Only the *public* key (advertisement key) is stored on-device.  The private
 * key never leaves the owner's phone/Mac.  If the device is physically captured,
 * the attacker only gets the public key — they cannot decrypt location reports.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Length of an Apple Find My advertisement public key (SECP224R1 x-coordinate) */
#define FINDMY_PUBLIC_KEY_LEN   28

/**
 * Default public key (Base64-encoded, 28 bytes decoded).
 *
 * Generate with:  python generate_keys.py
 * Paste the "Public key (Base64):" output here.
 *
 * Set to all-zeros to ship without a key (NVS-only provisioning).
 */
#define FINDMY_DEFAULT_PUBLIC_KEY_B64  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

/**
 * Load the active public key.
 *
 * Priority:
 *   1. NVS key (if previously set via findmy_keys_set)
 *   2. Compiled-in default (FINDMY_DEFAULT_PUBLIC_KEY_B64)
 *
 * @param[out] pub_key_out  28-byte buffer to receive the public key
 * @return  true   A valid (non-zero) key was loaded
 * @return  false  No key available (both NVS and default are empty/zero)
 */
bool findmy_keys_load(uint8_t pub_key_out[FINDMY_PUBLIC_KEY_LEN]);

/**
 * Store a new public key in NVS (persists across reboots and OTA updates).
 *
 * @param pub_key  28-byte public key (SECP224R1 x-coordinate)
 * @return ESP_OK on success
 */
esp_err_t findmy_keys_set(const uint8_t pub_key[FINDMY_PUBLIC_KEY_LEN]);

/**
 * Erase the NVS key (factory reset).
 * Next boot will use the compiled-in default.
 *
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no key was stored
 */
esp_err_t findmy_keys_erase(void);

#ifdef __cplusplus
}
#endif
