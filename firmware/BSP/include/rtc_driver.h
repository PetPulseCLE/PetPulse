/**
 * @file rtc_driver.h
 * @brief Real-Time Clock driver for ESP32-S3.
 *
 * Uses the ESP32-S3 internal RTC backed by an external 32.768 kHz crystal
 * on XTAL_32K_P (GPIO15) / XTAL_32K_N (GPIO16). Provides wall-clock time
 * with sub-second precision, timezone/DST support, and persistence across
 * deep-sleep.
 *
 * Time can be set locally or synchronized from a phone app via the BLE
 * Current Time Service (CTS) — see ble_driver.h for the CTS write handler.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

/** Default timezone string (POSIX TZ format).
 *  UTC by default — set via rtc_set_timezone() at runtime. */
#define RTC_DEFAULT_TIMEZONE    "UTC0"

/** NVS namespace for persisting timezone */
#define RTC_NVS_NAMESPACE       "rtc"
#define RTC_NVS_KEY_TZ          "tz"

/* ========================================================================== */
/*  Date/Time Structure                                                       */
/* ========================================================================== */

/** Simplified date-time for easy use without struct tm gymnastics. */
typedef struct {
    uint16_t year;      /**< Full year (e.g. 2026) */
    uint8_t  month;     /**< 1–12 */
    uint8_t  day;       /**< 1–31 */
    uint8_t  hour;      /**< 0–23 */
    uint8_t  minute;    /**< 0–59 */
    uint8_t  second;    /**< 0–59 */
    uint8_t  day_of_week; /**< 0 = Sunday, 6 = Saturday */
} rtc_datetime_t;

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

/**
 * @brief Initialize the RTC subsystem.
 *
 * Restores timezone from NVS if previously saved. The 32.768 kHz external
 * crystal is selected at boot via sdkconfig (CONFIG_RTC_CLK_SRC_EXT_CRYS).
 *
 * @return ESP_OK on success.
 */
esp_err_t rtc_driver_init(void);

/* ========================================================================== */
/*  Time Get / Set                                                            */
/* ========================================================================== */

/**
 * @brief Get the current time as a Unix timestamp (UTC).
 * @return Seconds since 1970-01-01 00:00:00 UTC.
 */
time_t rtc_get_timestamp(void);

/**
 * @brief Get the current local date and time.
 * @param[out] dt  Filled with local time components.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if dt is NULL.
 */
esp_err_t rtc_get_datetime(rtc_datetime_t *dt);

/**
 * @brief Get the current UTC date and time.
 * @param[out] dt  Filled with UTC time components.
 * @return ESP_OK on success.
 */
esp_err_t rtc_get_datetime_utc(rtc_datetime_t *dt);

/**
 * @brief Set the RTC to a specific UTC Unix timestamp.
 *
 * Typically called after BLE time sync from phone.
 *
 * @param timestamp  Seconds since epoch (UTC).
 * @return ESP_OK on success.
 */
esp_err_t rtc_set_timestamp(time_t timestamp);

/**
 * @brief Set the RTC from a date-time struct (interpreted as UTC).
 * @param[in] dt  UTC date and time to set.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if dt is NULL or invalid.
 */
esp_err_t rtc_set_datetime_utc(const rtc_datetime_t *dt);

/* ========================================================================== */
/*  BLE CTS (Current Time Service) Helpers                                    */
/* ========================================================================== */

/**
 * @brief Set the RTC from a BLE Current Time Service (CTS) payload.
 *
 * The CTS Current Time characteristic (0x2A2B) is a 10-byte structure:
 *   [0-1] year (uint16 LE), [2] month, [3] day, [4] hour,
 *   [5] minute, [6] second, [7] day_of_week, [8] fractions256,
 *   [9] adjust_reason.
 *
 * @param data     Pointer to the raw 10-byte CTS payload.
 * @param data_len Length (must be >= 10).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad data.
 */
esp_err_t rtc_set_from_ble_cts(const uint8_t *data, size_t data_len);

/**
 * @brief Build a BLE CTS Current Time characteristic value (10 bytes).
 * @param[out] buf   Output buffer (must be >= 10 bytes).
 * @param[out] len   Set to 10 on success.
 * @return ESP_OK on success.
 */
esp_err_t rtc_get_ble_cts(uint8_t *buf, size_t *len);

/* ========================================================================== */
/*  Timezone                                                                  */
/* ========================================================================== */

/**
 * @brief Set the timezone using a POSIX TZ string.
 *
 * Examples:
 *   "EST5EDT,M3.2.0,M11.1.0"   → US Eastern
 *   "CST-8"                      → China Standard Time
 *   "AEST-10AEDT,M10.1.0,M4.1.0" → Sydney
 *
 * The timezone is persisted in NVS and restored on next boot.
 *
 * @param tz_str  POSIX timezone string (max 63 chars).
 * @return ESP_OK on success.
 */
esp_err_t rtc_set_timezone(const char *tz_str);

/**
 * @brief Get the currently configured timezone string.
 * @param[out] buf      Buffer to receive the string.
 * @param      buf_len  Size of buffer.
 * @return ESP_OK on success.
 */
esp_err_t rtc_get_timezone(char *buf, size_t buf_len);

/* ========================================================================== */
/*  Utilities                                                                 */
/* ========================================================================== */

/**
 * @brief Check if the RTC has been set at least once since boot.
 *
 * Before the first `rtc_set_*()` call, the RTC reads as epoch (1970).
 * Use this to decide whether to display "time not set" in the UI.
 *
 * @return true if time has been explicitly set.
 */
bool rtc_is_time_set(void);

/**
 * @brief Get uptime in seconds since boot (monotonic, unaffected by time set).
 * @return Uptime in seconds.
 */
uint32_t rtc_get_uptime_sec(void);

/**
 * @brief Format the current local time into a string.
 * @param[out] buf      Output buffer.
 * @param      buf_len  Size of output buffer.
 * @param      fmt      strftime format string (e.g. "%Y-%m-%d %H:%M:%S").
 * @return ESP_OK on success.
 */
esp_err_t rtc_strftime(char *buf, size_t buf_len, const char *fmt);

#ifdef __cplusplus
}
#endif
