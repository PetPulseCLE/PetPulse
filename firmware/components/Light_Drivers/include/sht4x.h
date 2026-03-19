#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include <stdint.h>
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

// SHT40-AD1F-R2 I2C Address
#define SHT40_ADDRESS 0x44 

// Configuration
#define SHT40_I2C_PORT BSP_I2C_PORT

typedef enum {
    SHT4X_HIGH_PRECISION   = 0xFD,
    SHT4X_MEDIUM_PRECISION = 0xF6,
    SHT4X_LOW_PRECISION    = 0xE0,
} sht4x_precision_t;

typedef enum {
    SHT4X_HEATER_200MW_1S   = 0x39,
    SHT4X_HEATER_200MW_0_1S = 0x32,
    SHT4X_HEATER_110MW_1S   = 0x2F,
    SHT4X_HEATER_110MW_0_1S = 0x24,
    SHT4X_HEATER_20MW_1S    = 0x1E,
    SHT4X_HEATER_20MW_0_1S  = 0x15,
} sht4x_heater_t;

typedef struct {
    float temperature;      // Degrees Celsius
    float humidity;         // Percent RH
    uint32_t serial_number; // Unique Serial Number
} sht4x_data_t;

/**
 * Initialize SHT4x.
 * Performs a soft reset and reads the serial number to verify presence.
 */
esp_err_t sht4x_init(sht4x_data_t *handle_data);

/**
 * Perform a measurement.
 * This is a blocking call. Duration depends on precision (max ~10ms).
 *
 * @param precision Measurement precision/repeatability
 * @param data Pointer to struct where results will be stored
 */
esp_err_t sht4x_measure(sht4x_precision_t precision, sht4x_data_t *data);

/**
 * Activate internal heater and perform a measurement.
 * Used for removing condensation or creep.
 * WARNING: Do not operate heater >10% duty cycle.
 *
 * @param heater_mode Power and duration setting
 * @param data Pointer to struct where results will be stored
 */
esp_err_t sht4x_activate_heater(sht4x_heater_t heater_mode, sht4x_data_t *data);

/**
 * Perform Soft Reset.
 */
esp_err_t sht4x_soft_reset(void);

#ifdef __cplusplus
}
#endif