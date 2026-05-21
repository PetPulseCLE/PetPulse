/**
 * @file env_driver.h
 * @brief Environmental sensor driver — Sensirion SHT4x temperature + humidity.
 *
 * Manages a background FreeRTOS task that periodically samples the SHT4x over
 * the shared I2C bus and publishes readings to the BLE environmental
 * characteristic.  Publishing is gated on BLE authentication + RTC time-set
 * so that timestamps are meaningful and only bonded centrals receive data.
 *
 * Ambient temperature and humidity change slowly — the default 30 s sample
 * period is sufficient for pet monitoring while keeping bus utilisation and
 * power draw negligible.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sht4x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Tunables                                                                  */
/* ========================================================================== */

/** Default sample period (seconds). */
#define ENV_DEFAULT_PERIOD_S            30

/** Task stack (bytes).  Only a single 6-byte I2C buffer is on-stack. */
#define ENV_TASK_STACK                  3072

/** Task priority.  Below IMU motion (3) / radar (5); above idle housekeeping. */
#define ENV_TASK_PRIORITY               2

/** Consecutive measurement failures before attempting a soft-reset + re-init. */
#define ENV_MAX_FAIL_BEFORE_RECOVERY    5

/** Clamp range for the sample period (seconds). */
#define ENV_MIN_PERIOD_S                1
#define ENV_MAX_PERIOD_S                3600

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

typedef struct {
    uint32_t          sample_period_s;     /**< Measurement period (s).  Clamped to [1, 3600]. */
    sht4x_precision_t precision;           /**< SHT4x measurement precision. */
    bool              gate_on_ble_auth;    /**< Wait for BLE auth + RTC before publishing. */
    bool              notify_on_publish;   /**< Trigger BLE notify when value changes. */
} env_driver_config_t;

/** Production defaults. */
#define ENV_DRIVER_CONFIG_DEFAULT() { \
    .sample_period_s   = ENV_DEFAULT_PERIOD_S, \
    .precision         = SHT4X_HIGH_PRECISION, \
    .gate_on_ble_auth  = true, \
    .notify_on_publish = true, \
}

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

/**
 * @brief Initialise the SHT4x sensor and start the env monitoring task.
 *
 * Call once after bsp_bus_init() and ble_driver_init().  The task runs until
 * env_driver_stop() is called or the scheduler shuts down.
 *
 * @param config  Configuration; NULL selects production defaults.
 * @return ESP_OK                   Task running.
 * @return ESP_ERR_INVALID_STATE    Already started, or I2C bus not initialised.
 * @return ESP_ERR_NO_MEM           Task / mutex allocation failed.
 * @return other                    Sensor init error forwarded from sht4x_init().
 */
esp_err_t env_driver_start(const env_driver_config_t *config);

/**
 * @brief Request task shutdown and wait (bounded) for the task to exit.
 *        Safe to call if the driver was never started.
 */
esp_err_t env_driver_stop(void);

/* ========================================================================== */
/*  Telemetry                                                                 */
/* ========================================================================== */

/** True once at least one valid measurement has been cached. */
bool env_driver_has_reading(void);

/**
 * @brief Copy the most recent valid reading into caller-supplied buffers.
 *
 * @param[out] temperature_c  Temperature in degrees Celsius (may be NULL).
 * @param[out] humidity_pct   Relative humidity 0-100 % (may be NULL).
 * @return ESP_OK                   Reading copied.
 * @return ESP_ERR_INVALID_STATE    No valid reading yet.
 */
esp_err_t env_driver_get_reading(float *temperature_c, float *humidity_pct);

#ifdef __cplusplus
}
#endif
