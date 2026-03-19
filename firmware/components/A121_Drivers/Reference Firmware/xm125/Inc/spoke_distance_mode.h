// Copyright (c) 2025 Sensor Bar Project
// All rights reserved

#ifndef SPOKE_DISTANCE_MODE_H_
#define SPOKE_DISTANCE_MODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "acc_sensor.h"
#include "acc_detector_distance.h"
#include "spoke_i2c_protocol.h"

/**
 * @brief Initialize Distance Mode
 * @param sensor Acconeer sensor handle
 * @return true if successful
 */
bool spoke_distance_init(acc_sensor_t *sensor);

/**
 * @brief Cleanup Distance Mode resources
 */
void spoke_distance_cleanup(void);

/**
 * @brief Update distance configuration from I2C registers
 * @param config Configuration from I2C
 * @return true if successful
 */
bool spoke_distance_update_config(const spoke_distance_config_t *config);

/**
 * @brief Perform sensor calibration for distance mode
 * @param sensor Acconeer sensor handle
 * @return true if successful
 */
bool spoke_distance_calibrate(acc_sensor_t *sensor);

/**
 * @brief Perform a distance measurement
 * @param sensor Acconeer sensor handle
 * @param result Pointer to store results
 * @return true if successful
 */
bool spoke_distance_measure(acc_sensor_t *sensor, spoke_distance_result_t *result);

/**
 * @brief Check if distance mode is ready
 * @return true if ready for measurements
 */
bool spoke_distance_is_ready(void);

/**
 * @brief Get last error code
 * @return error code
 */
uint8_t spoke_distance_get_error(void);

/**
 * @brief Get current temperature from sensor
 * @return temperature in degrees Celsius
 */
int16_t spoke_distance_get_temperature(void);

/**
 * @brief Get temperature compensation factor
 * @return signal adjustment factor (1.0 = no compensation)
 */
float spoke_distance_get_temp_compensation(void);

#endif // SPOKE_DISTANCE_MODE_H_
