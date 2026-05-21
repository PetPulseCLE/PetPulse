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
#include "rtc_driver.h"
#include "bms_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration Constants                                                   */
/* ========================================================================== */

/** Maximum BLE device name length */
#define BLE_DEVICE_NAME_MAX_LEN  32

/** Default BLE device name */
#define BLE_DEVICE_NAME_DEFAULT  "PetPulse-0001"

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
    BLE_EVENT_TIME_SYNCED,
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

/* ========================================================================== */
/*  Telemetry Data Structures (from PetPulse Custom Profile)                  */
/* ========================================================================== */

typedef struct {
    float x;
    float y;
    float z;
    uint8_t accuracy;
} __attribute__((packed)) ble_accel_t;

typedef struct {
    float x;
    float y;
    float z;
    uint8_t accuracy;
} __attribute__((packed)) ble_gyro_t;

typedef struct {
    float x;
    float y;
    float z;
    uint8_t accuracy;
} __attribute__((packed)) ble_magf_t;

typedef struct {
    float real;
    float x;
    float y;
    float z;
    float rad_accuracy;
    uint8_t accuracy;
} __attribute__((packed)) ble_rv_t;

typedef struct {
    uint32_t latency;
    uint16_t steps;
    uint8_t accuracy;
} __attribute__((packed)) ble_step_count_t;

typedef struct {
    uint8_t confidence[10];
    uint8_t mostLikelyState;
    uint8_t accuracy;
} __attribute__((packed)) ble_activity_class_t;

typedef struct {
    uint8_t breath_rate;
    uint8_t heart_rate;
    uint8_t hr_confidence;
    rtc_datetime_t timestamp;
} __attribute__((packed)) ble_vitals_t;

typedef struct {
    float temperature;
    float humidity;
    rtc_datetime_t timestamp;
} __attribute__((packed)) ble_env_t;

typedef struct {
    ble_step_count_t stepCount;
    ble_activity_class_t activityClass;
    rtc_datetime_t timestamp;
} __attribute__((packed)) ble_activity_t;

typedef struct {
    ble_accel_t accel;
    ble_gyro_t gyro;
    ble_magf_t magf;
    ble_rv_t rv;
    rtc_datetime_t timestamp;
} __attribute__((packed)) ble_raw_t;

typedef struct {
    uint8_t presence_bitmask;
    ble_raw_t raw;
    ble_activity_t activity;
    ble_vitals_t vitals;
    ble_env_t env;
} __attribute__((packed)) ble_aggregated_all_t;

#define BLE_AGG_RAW_PRESENT_BIT       (1 << 0)
#define BLE_AGG_ACTIVITY_PRESENT_BIT  (1 << 1)
#define BLE_AGG_VITALS_PRESENT_BIT    (1 << 2)
#define BLE_AGG_ENV_PRESENT_BIT       (1 << 3)

typedef enum {
    BLE_MODE_BACKGROUND = 0,
    BLE_MODE_LIVE       = 1,
    BLE_MODE_DEV        = 2
} ble_mode_t;

/* ========================================================================== */
/*  Custom GATT UUIDs (128-bit little-endian)                                 */
/* ========================================================================== */

/* Base UUID: 792Cxxxx-7B95-4A4D-8BC2-6D04809BB406 */
#define BLE_CUSTOM_UUID_BASE(byte0, byte1) \
    0x06, 0xB4, 0x9B, 0x80, 0x04, 0x6D, 0xC2, 0x8B, \
    0x4D, 0x4A, 0x95, 0x7B, byte0, byte1, 0x2C, 0x79

/* Services */
#define BLE_SVC_VITALS_UUID128     BLE_CUSTOM_UUID_BASE(0xE0, 0x45)
#define BLE_SVC_MOTION_UUID128     BLE_CUSTOM_UUID_BASE(0xE1, 0x45)
#define BLE_SVC_ENVIRO_UUID128     BLE_CUSTOM_UUID_BASE(0xE2, 0x45)
#define BLE_SVC_AGGREGATED_UUID128 BLE_CUSTOM_UUID_BASE(0xE9, 0x45)

/* Characteristics */
#define BLE_CHR_VITALS_UUID128     BLE_CUSTOM_UUID_BASE(0xE3, 0x45)
#define BLE_CHR_RAW_UUID128        BLE_CUSTOM_UUID_BASE(0xE4, 0x45)
#define BLE_CHR_ACTIVITY_UUID128   BLE_CUSTOM_UUID_BASE(0xE5, 0x45)
#define BLE_CHR_MODE_UUID128       BLE_CUSTOM_UUID_BASE(0xE6, 0x45)
#define BLE_CHR_AUTH_PING_UUID128  BLE_CUSTOM_UUID_BASE(0xE7, 0x45)
#define BLE_CHR_ENV_UUID128        BLE_CUSTOM_UUID_BASE(0xE8, 0x45)
#define BLE_CHR_AGGREGATED_UUID128 BLE_CUSTOM_UUID_BASE(0xEA, 0x45)

/* 16-bit Service UUIDs (Standard) */
#define BLE_SVC_BATTERY_UUID16     0x180F
#define BLE_SVC_CUR_TIME_UUID16    0x1805

/* 16-bit Characteristic UUIDs */
#define BLE_CHR_BATT_LEVEL_UUID16       0x2A19
#define BLE_CHR_CUR_TIME_UUID16         0x2A2B
#define BLE_CHR_BATT_LEVEL_STAT_UUID16  0x2BED
#define BLE_CHR_BATT_ENERGY_STAT_UUID16 0x2BF0
#define BLE_CHR_BATT_TIME_STAT_UUID16   0x2BEE
#define BLE_CHR_BATT_HEALTH_STAT_UUID16 0x2BEA


/* Wire-format structs for BAS 1.1 extended battery characteristics.
 * Field presence is pinned to the `flags` value chosen at init — do not change
 * `flags` without matching the struct layout, or the central will mis-decode. */

/* 0x2BED Battery Level Status — flags = 0x06 (Battery Level + Additional Status). */
typedef struct {
    uint8_t  flags;             /* bit0 Identifier, bit1 Battery Level, bit2 Additional Status */
    uint16_t power_state;       /* see BAS 1.1 Power State bit field */
    uint8_t  battery_level;     /* 0-100 %, present iff flags bit1 */
    uint8_t  additional_status; /* bits0-1 Service Required, bit2 Battery Fault */
} __attribute__((packed)) ble_batt_level_stat_t;

/* 0x2BF0 Battery Energy Status — flags = 0x02 (Present Voltage only).
 * curr_voltage is IEEE-11073 16-bit SFLOAT (medfloat16), unit = volts. */
typedef struct {
    uint8_t  flags;
    uint16_t curr_voltage; /* SFLOAT volts; use ble_driver_batt_update_curr_voltage() to encode from mV */
} __attribute__((packed)) ble_batt_energy_stat_t;

/* 0x2BEE Battery Time Status — flags = 0x02 (Time Until Recharged present).
 * Time Until Discharged is MANDATORY per spec (always present).
 * All values are uint24 little-endian minutes; 0xFFFFFF = Unknown, 0xFFFFFE = >0xFFFFFD. */
typedef struct {
    uint8_t flags;
    uint8_t time_to_discharge[3]; /* mandatory */
    uint8_t time_to_recharge[3];  /* present iff flags bit1 */
} __attribute__((packed)) ble_batt_time_stat_t;

/* 0x2BEA Battery Health Status — flags = 0x05 (Health Summary + Current Temperature). */
typedef struct {
    uint8_t flags;
    uint8_t health_summary; /* 0-100 %, present iff flags bit0 */
    int8_t  current_temp;   /* °C, present iff flags bit2; 0x7F=>126°C, 0x80=<-127°C */
} __attribute__((packed)) ble_batt_health_stat_t;


/* ========================================================================== */
/*  Piecemeal Component Setters                                               */
/* ========================================================================== */
esp_err_t ble_driver_set_accel(const ble_accel_t *accel);
esp_err_t ble_driver_set_gyro(const ble_gyro_t *gyro);
esp_err_t ble_driver_set_magf(const ble_magf_t *magf);
esp_err_t ble_driver_set_rv(const ble_rv_t *rv);
esp_err_t ble_driver_set_step_count(const ble_step_count_t *step);
esp_err_t ble_driver_set_activity_class(const ble_activity_class_t *act_class);
esp_err_t ble_driver_set_hr(uint8_t hr, uint8_t hr_acc);
esp_err_t ble_driver_set_br(uint8_t br);
esp_err_t ble_driver_set_temp(float temp);
esp_err_t ble_driver_set_humidity(float hum);

/* ========================================================================== */
/*  Auto-Timestamp Dispatchers                                                */
/* ========================================================================== */
esp_err_t ble_driver_notify_vitals(bool notify);
esp_err_t ble_driver_notify_raw(bool notify);
esp_err_t ble_driver_notify_activity(bool notify);
esp_err_t ble_driver_notify_env(bool notify);
esp_err_t ble_driver_notify_aggregated(bool notify);

/* ========================================================================== */
/*  Connection & Metric Polling                                               */
/* ========================================================================== */
bool ble_driver_is_authenticated(void);
int8_t ble_driver_get_rssi(void);
void ble_driver_set_tx_power(int8_t tx_power);

/* ========================================================================== */
/*  Telemetry Setters & Getters                                               */
/* ========================================================================== */

esp_err_t ble_driver_update_vitals(const ble_vitals_t *vitals, bool notify);
esp_err_t ble_driver_update_raw_motion(const ble_raw_t *raw, bool notify);
esp_err_t ble_driver_update_activity(const ble_activity_t *activity, bool notify);
esp_err_t ble_driver_update_env(const ble_env_t *env, bool notify);
esp_err_t ble_driver_update_aggregated(const ble_aggregated_all_t *agg, bool notify);


esp_err_t ble_driver_update_batt_level_stat(const ble_batt_level_stat_t *stat, bool notify);
esp_err_t ble_driver_update_batt_energy_stat(const ble_batt_energy_stat_t *stat, bool notify);
esp_err_t ble_driver_update_batt_time_stat(const ble_batt_time_stat_t *stat, bool notify);
esp_err_t ble_driver_update_batt_health_stat(const ble_batt_health_stat_t *stat, bool notify);

/**
 * Bulk updates the extended battery characteristics (0x2BED, 0x2BF0, 0x2BEE, 0x2BEA) atomically
 * based on a BMS driver snapshot, triggering notifications as appropriate.
 */
esp_err_t ble_driver_sync_extended_battery(const power_snapshot_t *snap);

/* Bitwise Battery Setters for matching C++ driver functionality */
void ble_driver_batt_update_power_state(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault);
void ble_driver_batt_update_level(uint8_t battery_level);
void ble_driver_batt_update_additional_status(uint8_t service_req, uint8_t batt_fault);
/* Accepts millivolts; encodes to IEEE-11073 SFLOAT (volts) for on-wire storage. */
void ble_driver_batt_update_curr_voltage(uint16_t mv);
void ble_driver_batt_update_time_discharge(const uint8_t time_to_discharge[3]);
void ble_driver_batt_update_time_recharge(const uint8_t time_to_recharge[3]);
void ble_driver_batt_update_health_summary(uint8_t health_summary);
void ble_driver_batt_update_current_temp(int8_t current_temp);

esp_err_t ble_driver_set_mode(ble_mode_t mode);
ble_mode_t ble_driver_get_mode(void);

#ifdef __cplusplus
}
#endif
