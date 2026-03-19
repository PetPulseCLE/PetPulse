/**
 * @file ble_driver.h
 * @brief BLE (NimBLE) driver for ESP32-S3 — GAP advertising, GATT server,
 *        security/pairing, and OOB data for NFC tap-to-pair.
 *
 * Compatible with both iOS (iPhone) and Android devices.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration Constants                                                   */
/* ========================================================================== */

/** Maximum BLE device name length */
#define BLE_DEVICE_NAME_MAX_LEN  32

/** Default BLE device name */
#define BLE_DEVICE_NAME_DEFAULT  "PetPulse"

/** BLE Appearance: Generic Heart Rate Sensor */
#define BLE_APPEARANCE_HEART_RATE  0x0340

/** BLE Appearance: Generic tag */
#define BLE_APPEARANCE_GENERIC_TAG 0x0200

/* ========================================================================== */
/*  Event Types                                                               */
/* ========================================================================== */

typedef enum {
    BLE_EVENT_CONNECTED,
    BLE_EVENT_DISCONNECTED,
    BLE_EVENT_PAIRED,
    BLE_EVENT_PAIRING_FAILED,
    BLE_EVENT_MTU_CHANGED,
    BLE_EVENT_ADV_STARTED,
    BLE_EVENT_ADV_STOPPED,
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint16_t conn_handle;
    union {
        struct {
            uint8_t peer_addr[6];
            uint8_t addr_type;
        } connected;
        struct {
            int reason;
        } disconnected;
        struct {
            bool bonded;
            bool encrypted;
        } paired;
        struct {
            uint16_t mtu;
        } mtu_changed;
    };
} ble_event_t;

/** User event callback */
typedef void (*ble_event_cb_t)(const ble_event_t *event, void *user_data);

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

typedef struct {
    const char *device_name;          /**< BLE device name (NULL → default) */
    ble_event_cb_t event_cb;          /**< Event callback (optional) */
    void *user_data;                  /**< User data passed to callback */
    uint16_t appearance;              /**< BLE appearance value */
    bool enable_bonding;              /**< Store bonds in NVS */
    bool require_mitm;                /**< Require MITM protection (false = Just Works) */
} ble_config_t;

/** Sensible defaults */
#define BLE_CONFIG_DEFAULT() { \
    .device_name = BLE_DEVICE_NAME_DEFAULT, \
    .event_cb = NULL, \
    .user_data = NULL, \
    .appearance = BLE_APPEARANCE_GENERIC_TAG, \
    .enable_bonding = true, \
    .require_mitm = false, \
}

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

/**
 * @brief Initialize the BLE stack (NimBLE), configure GAP/GATT/Security,
 *        and start the host task.  Advertising begins automatically.
 */
esp_err_t ble_driver_init(const ble_config_t *config);

/**
 * @brief Tear down BLE — stop advertising, disconnect, and deinit NimBLE.
 */
esp_err_t ble_driver_deinit(void);

/* ========================================================================== */
/*  Advertising                                                               */
/* ========================================================================== */

/** Start connectable undirected advertising */
esp_err_t ble_driver_start_advertising(void);

/** Stop advertising */
esp_err_t ble_driver_stop_advertising(void);

/** Check if advertising is currently active */
bool ble_driver_is_advertising(void);

/* ========================================================================== */
/*  Connection                                                                */
/* ========================================================================== */

/** Check if a central is connected */
bool ble_driver_is_connected(void);

/** Get the current connection handle (0xFFFF if none) */
uint16_t ble_driver_get_conn_handle(void);

/** Disconnect the current connection */
esp_err_t ble_driver_disconnect(void);

/* ========================================================================== */
/*  Address & Identity                                                        */
/* ========================================================================== */

/**
 * @brief Get the BLE device address (used for NFC handover).
 * @param addr   Output: 6-byte address (little-endian per BT spec)
 * @param addr_type  Output: 0 = public, 1 = random
 */
esp_err_t ble_driver_get_address(uint8_t *addr, uint8_t *addr_type);

/** Get the configured device name */
const char *ble_driver_get_device_name(void);

/** Get the configured appearance value */
uint16_t ble_driver_get_appearance(void);

/* ========================================================================== */
/*  Security / OOB                                                            */
/* ========================================================================== */

/**
 * @brief Generate a 16-byte Temporary Key for OOB pairing.
 *        This TK can be embedded in the NFC NDEF record so the phone
 *        uses OOB pairing instead of Just Works.
 * @param tk  Output: 16-byte TK value (caller supplies buffer)
 */
esp_err_t ble_driver_generate_oob_tk(uint8_t *tk);

/** Erase all stored bonds */
esp_err_t ble_driver_clear_bonds(void);

/* ========================================================================== */
/*  Battery Service (BAS)                                                     */
/* ========================================================================== */

/**
 * @brief Notify the connected central of a battery level change.
 *
 * Sends a BLE Battery Level (0x2A19) notification.  Duplicate values are
 * suppressed internally — safe to call on every snapshot update.
 *
 * @param soc_pct  State of charge 0-100 % (clamped if > 100)
 * @return ESP_OK              Notification sent (or suppressed as duplicate)
 * @return ESP_ERR_INVALID_STATE  No BLE connection active
 * @return ESP_ERR_NO_MEM      mbuf allocation failed
 * @return ESP_FAIL            NimBLE notify error (e.g., not subscribed)
 */
esp_err_t ble_driver_notify_battery_level(uint8_t soc_pct);

#ifdef __cplusplus
}
#endif
