/**
 * @file nfc_pairing.h
 * @brief NFC tap-to-pair: presents BLE Handover Select via Type 4 Tag emulation.
 *
 * Runs as a FreeRTOS task in ISO14443A listener mode. When a phone taps,
 * the ST25R3916 handles anticollision automatically (PT Memory A). Then this
 * module handles ISO-DEP (ISO14443-4) framing and serves NDEF data containing
 * BLE Connection Handover records. The phone reads the BLE OOB data and
 * initiates a BLE connection.
 *
 * Compatible with iPhone (iOS 14+) and Android (4.0+).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

typedef struct {
    uint8_t  uid[7];            /**< NFC UID (4 or 7 bytes) */
    uint8_t  uid_len;           /**< UID length (4 or 7) */
    uint8_t  atqa[2];           /**< ATQA response bytes */
    uint8_t  sak;               /**< SAK byte (0x20 = ISO-DEP support) */
    bool     include_oob_tk;    /**< Include OOB TK in NDEF for secure pairing */
    uint16_t task_stack_size;   /**< Pairing task stack size (0 = default 4096) */
    uint8_t  task_priority;     /**< Pairing task priority (0 = default 5) */
} nfc_pairing_config_t;

/** Sensible defaults for NFC pairing */
#define NFC_PAIRING_CONFIG_DEFAULT() { \
    .uid = {0x04, 0x51, 0x7C, 0xA2, 0x3E, 0x8B, 0x90}, \
    .uid_len = 7, \
    .atqa = {0x44, 0x00}, \
    .sak = 0x20, /* ISO-DEP support */ \
    .include_oob_tk = false, \
    .task_stack_size = 8192, \
    .task_priority = 5, \
}

/* ========================================================================== */
/*  Lifecycle API                                                             */
/* ========================================================================== */

/**
 * @brief Start the NFC tap-to-pair service.
 *
 * Creates a new FreeRTOS task that:
 *  1. Configures ST25R3916 in ISO14443A listener mode
 *  2. Sets up collision resolution data (UID/ATQA/SAK)
 *  3. Builds NDEF Handover Select message with BLE OOB data
 *  4. Waits for phone taps and serves NDEF via Type 4 Tag emulation
 *
 * Requires both nfc_init() and ble_driver_init() to have been called first.
 *
 * @param config  Pairing configuration (NULL → defaults)
 * @return ESP_OK on success
 */
esp_err_t nfc_pairing_start(const nfc_pairing_config_t *config);

/**
 * @brief Stop the NFC tap-to-pair service.
 *
 * Signals the pairing task to exit, waits for it, and restores NFC state.
 */
esp_err_t nfc_pairing_stop(void);

/**
 * @brief Check if the pairing service is running.
 */
bool nfc_pairing_is_running(void);

/**
 * @brief Force a rebuild of the NDEF message.
 *
 * Call this if BLE address or name changed after start.
 */
esp_err_t nfc_pairing_refresh_ndef(void);

#ifdef __cplusplus
}
#endif
