// Copyright (c) Acconeer AB, 2023
// All rights reserved

#ifndef I2C_REF_APP_BREATHING_H_
#define I2C_REF_APP_BREATHING_H_

#include <stdbool.h>
#include <stdint.h>

#include "ref_app_breathing.h"


/**
 * @brief Get ref app breathing configuration handle
 *
 * @return ref app breathing configuration handle
 */
ref_app_breathing_config_t *i2c_ref_app_breathing_get_config(void);


/**
 * @brief Send command to be executed to i2c ref app breathing
 *
 * @param[in] command The command to be executed
 * @return true if successful
 */
bool i2c_ref_app_breathing_command(uint32_t command);


/**
 * @brief Get ref app breathing status
 *
 * @return status from ref app breathing
 */
uint32_t i2c_ref_app_breathing_get_status(void);


/**
 * @brief Get ref app breathing result
 *
 * @return result from ref app breathing
 */
uint32_t i2c_ref_app_breathing_get_result(void);


/**
 * @brief Get breathing rate
 *
 * @return The breathing rate
 */
float i2c_ref_app_breathing_get_breathing_rate(void);


/**
 * @brief Get heart rate
 *
 * @return The heart rate
 */
float i2c_ref_app_breathing_get_heart_rate(void);


/**
 * @brief Get heart rate confidence
 *
 * @return The heart rate confidence
 */
float i2c_ref_app_breathing_get_heart_rate_confidence(void);


/**
 * @brief Get detected distance
 *
 * @return The detected distance in meters
 */
float i2c_ref_app_breathing_get_detected_distance(void);


/**
 * @brief Get app state
 *
 * @return A state of the application
 */
ref_app_breathing_app_state_t i2c_ref_app_breathing_get_app_state(void);


/**
 * @brief Get measure counter
 *
 * The counter will be increased every time the application completes a measure
 *
 * @return measure counter from the application
 */
uint32_t i2c_ref_app_breathing_get_counter(void);

/**
 * @brief Get raw data word (32-bit) at index
 *
 * @param[in] index The index of the word (complex point)
 * @return The raw data word (packed int16_t real, int16_t imag)
 */
uint32_t i2c_ref_app_breathing_get_raw_data_word(uint16_t index);

/**
 * @brief Get raw data length
 *
 * @return The number of complex points in the raw data buffer
 */
uint32_t i2c_ref_app_breathing_get_raw_data_length(void);

/**
 * @brief Get the actual frame rate for the current ref app breathing configuration
 *
 * @return The frame rate in mHz
 */
uint32_t i2c_ref_app_breathing_get_frame_rate_mhz(void);


#endif
