// Copyright (c) 2025 Sensor Bar Project
// All rights reserved

#ifndef SPOKE_VITALS_MODE_H_
#define SPOKE_VITALS_MODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "acc_sensor.h"
#include "spoke_i2c_protocol.h"

/**
 * @brief Initialize Vitals Mode
 * @param sensor Acconeer sensor handle
 * @return true if successful
 */
bool spoke_vitals_init(acc_sensor_t *sensor);

/**
 * @brief Cleanup Vitals Mode resources
 */
void spoke_vitals_cleanup(void);

/**
 * @brief Update vitals configuration from I2C registers
 * @param config Configuration from I2C
 * @return true if successful
 */
bool spoke_vitals_update_config(const spoke_vitals_config_t *config);

/**
 * @brief Perform sensor calibration for vitals mode
 * @param sensor Acconeer sensor handle
 * @return true if successful
 */
bool spoke_vitals_calibrate(acc_sensor_t *sensor);

/**
 * @brief Start a vitals measurement (non-blocking)
 * @param sensor Acconeer sensor handle
 * @return true if started successfully
 */
bool spoke_vitals_start_measurement(acc_sensor_t *sensor);

/**
 * @brief Process vitals measurement (call periodically)
 * @param sensor Acconeer sensor handle
 * @param result Pointer to store results (updated when measurement completes)
 * @return true if measurement is complete
 */
bool spoke_vitals_process(acc_sensor_t *sensor, spoke_vitals_result_t *result);

/**
 * @brief Stop ongoing vitals measurement
 */
void spoke_vitals_stop_measurement(void);

/**
 * @brief Check if vitals mode is ready
 * @return true if ready for measurements
 */
bool spoke_vitals_is_ready(void);

/**
 * @brief Get measurement progress (0-100%)
 * @return progress percentage
 */
uint8_t spoke_vitals_get_progress(void);

/**
 * @brief Get last error code
 * @return error code
 */
uint8_t spoke_vitals_get_error(void);

/**
 * @brief Get current temperature from sensor
 * @return temperature in degrees Celsius
 */
int16_t spoke_vitals_get_temperature(void);

/**
 * @brief Get temperature compensation factor
 * @return signal adjustment factor (1.0 = no compensation)
 */
float spoke_vitals_get_temp_compensation(void);

#endif // SPOKE_VITALS_MODE_H_
