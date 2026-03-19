// Copyright (c) 2025 Sensor Bar Project
// All rights reserved

#ifndef SPOKE_I2C_PROTOCOL_H_
#define SPOKE_I2C_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Spoke I2C Register Map
 * 
 * This defines the complete register interface between the Hub (ESP32) and
 * each Spoke (XM125). The Hub writes to control registers and reads from
 * status/result registers.
 */

/* ============================================================================
 * SYSTEM CONTROL REGISTERS (0x00 - 0x0F)
 * ============================================================================ */
#define REG_DEVICE_ID           0x00  // Read-only: Device ID (0xA1)
#define REG_FIRMWARE_VERSION    0x01  // Read-only: Firmware version
#define REG_STATUS              0x02  // Read-only: Current status
#define REG_MODE                0x03  // Read/Write: Operating mode
#define REG_COMMAND             0x04  // Write: Command trigger
#define REG_ERROR_CODE          0x05  // Read-only: Last error code
#define REG_TEMPERATURE         0x06  // Read-only: Temperature (2 bytes, signed)
#define REG_CALIBRATION_STATUS  0x08  // Read-only: Calibration status flags

/* ============================================================================
 * DISTANCE MODE CONFIGURATION (0x10 - 0x2F)
 * ============================================================================ */
#define REG_DIST_START_M_INT    0x10  // Distance range start - integer part (m)
#define REG_DIST_START_M_FRAC   0x11  // Distance range start - fractional (cm)
#define REG_DIST_END_M_INT      0x12  // Distance range end - integer part (m)
#define REG_DIST_END_M_FRAC     0x13  // Distance range end - fractional (cm)
#define REG_DIST_MAX_PROFILE    0x14  // Maximum profile setting
#define REG_DIST_SIGNAL_QUALITY 0x15  // Signal quality (0-35 dB)
#define REG_DIST_THRESHOLD_METHOD 0x16 // Threshold method
#define REG_DIST_THRESHOLD_SENS 0x17  // Threshold sensitivity (0-100 = 0.0-1.0)
#define REG_DIST_PEAK_SORTING   0x18  // Peak sorting method
#define REG_DIST_MAX_NUM_TARGETS 0x19 // Max number of targets to report

/* ============================================================================
 * DISTANCE MODE RESULTS (0x30 - 0x5F)
 * ============================================================================ */
#define REG_DIST_NUM_TARGETS    0x30  // Number of detected targets
#define REG_DIST_RESULT_READY   0x31  // Result ready flag (1 = ready)

// Target 0 results (4 bytes per target: 2 bytes distance, 2 bytes strength)
#define REG_DIST_T0_DIST_H      0x32  // Target 0 Distance high byte (cm)
#define REG_DIST_T0_DIST_L      0x33  // Target 0 Distance low byte (cm)
#define REG_DIST_T0_STR_H       0x34  // Target 0 Strength high byte (dB * 10)
#define REG_DIST_T0_STR_L       0x35  // Target 0 Strength low byte (dB * 10)

// Target 1 results
#define REG_DIST_T1_DIST_H      0x36
#define REG_DIST_T1_DIST_L      0x37
#define REG_DIST_T1_STR_H       0x38
#define REG_DIST_T1_STR_L       0x39

// Target 2 results
#define REG_DIST_T2_DIST_H      0x3A
#define REG_DIST_T2_DIST_L      0x3B
#define REG_DIST_T2_STR_H       0x3C
#define REG_DIST_T2_STR_L       0x3D

// Target 3 results
#define REG_DIST_T3_DIST_H      0x3E
#define REG_DIST_T3_DIST_L      0x3F
#define REG_DIST_T3_STR_H       0x40
#define REG_DIST_T3_STR_L       0x41

// Target 4 results
#define REG_DIST_T4_DIST_H      0x42
#define REG_DIST_T4_DIST_L      0x43
#define REG_DIST_T4_STR_H       0x44
#define REG_DIST_T4_STR_L       0x45

// Additional targets can be added up to max 10 targets

/* ============================================================================
 * VITALS MODE CONFIGURATION (0x60 - 0x7F)
 * ============================================================================ */
#define REG_VITALS_TARGET_DIST_H  0x60  // Target distance high byte (cm)
#define REG_VITALS_TARGET_DIST_L  0x61  // Target distance low byte (cm)
#define REG_VITALS_MEAS_TIME_S    0x62  // Measurement time in seconds
#define REG_VITALS_HR_MIN         0x63  // Heart rate min (BPM)
#define REG_VITALS_HR_MAX         0x64  // Heart rate max (BPM)
#define REG_VITALS_RR_MIN         0x65  // Respiration rate min (BPM)
#define REG_VITALS_RR_MAX         0x66  // Respiration rate max (BPM)
#define REG_VITALS_HWAAS          0x67  // HWAAS setting (Hardware Accelerated Average Samples)
#define REG_VITALS_PROFILE        0x68  // Profile for vitals
#define REG_VITALS_STEP_LENGTH    0x69  // Step length

/* ============================================================================
 * VITALS MODE RESULTS (0x80 - 0x9F)
 * ============================================================================ */
#define REG_VITALS_RESULT_READY   0x80  // Result ready flag (1 = ready)
#define REG_VITALS_HR_BPM         0x81  // Heart rate (BPM)
#define REG_VITALS_RR_BPM         0x82  // Respiration rate (BPM)
#define REG_VITALS_MOTION_SCORE   0x83  // Motion/quality score (0-100)
#define REG_VITALS_CONFIDENCE_HR  0x84  // HR confidence (0-100)
#define REG_VITALS_CONFIDENCE_RR  0x85  // RR confidence (0-100)
#define REG_VITALS_PROGRESS       0x86  // Measurement progress (0-100%)

/* ============================================================================
 * REGISTER VALUE DEFINITIONS
 * ============================================================================ */

// Device ID value
#define DEVICE_ID_VALUE         0xA1

// REG_STATUS values
#define STATUS_IDLE             0x00
#define STATUS_INITIALIZING     0x01
#define STATUS_CALIBRATING      0x02
#define STATUS_READY            0x03
#define STATUS_MEASURING        0x04
#define STATUS_PROCESSING       0x05
#define STATUS_BUSY             0x06  // Processing command - do not send new commands
#define STATUS_ERROR            0xFF

// REG_MODE values
#define MODE_IDLE               0x00
#define MODE_DISTANCE           0x01
#define MODE_VITALS             0x02

// REG_COMMAND values
#define CMD_NOP                 0x00
#define CMD_RESET               0x01
#define CMD_CALIBRATE           0x02
#define CMD_START_MEASUREMENT   0x03
#define CMD_STOP_MEASUREMENT    0x04
#define CMD_CLEAR_RESULTS       0x05

// REG_ERROR_CODE values
#define ERROR_NONE              0x00
#define ERROR_NOT_CALIBRATED    0x01
#define ERROR_SENSOR_TIMEOUT    0x02
#define ERROR_INVALID_MODE      0x03
#define ERROR_INVALID_PARAM     0x04
#define ERROR_MEASUREMENT_FAILED 0x05
#define ERROR_I2C_ERROR         0x06

// Threshold methods (matches Acconeer API)
#define THRESHOLD_METHOD_FIXED_AMPLITUDE  0x00
#define THRESHOLD_METHOD_FIXED_STRENGTH   0x01
#define THRESHOLD_METHOD_RECORDED         0x02
#define THRESHOLD_METHOD_CFAR             0x03

// Peak sorting methods (matches Acconeer API)
#define PEAK_SORTING_STRONGEST  0x00
#define PEAK_SORTING_CLOSEST    0x01
#define PEAK_SORTING_FARTHEST   0x02

// Calibration status flags
#define CAL_STATUS_SENSOR_DONE    0x01
#define CAL_STATUS_DETECTOR_DONE  0x02
#define CAL_STATUS_NEEDED         0x80

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/**
 * @brief Distance measurement target result
 */
typedef struct {
    float distance_m;     // Distance in meters
    float strength_db;    // Signal strength in dB
} spoke_distance_target_t;

/**
 * @brief Distance mode results
 */
typedef struct {
    uint8_t num_targets;
    spoke_distance_target_t targets[10];
    bool result_ready;
} spoke_distance_result_t;

/**
 * @brief Vitals measurement result
 */
typedef struct {
    uint8_t heart_rate_bpm;
    uint8_t respiration_rate_bpm;
    uint8_t motion_score;
    uint8_t confidence_hr;
    uint8_t confidence_rr;
    uint8_t progress;
    bool result_ready;
} spoke_vitals_result_t;

/**
 * @brief Spoke system configuration
 */
typedef struct {
    uint8_t mode;
    uint8_t status;
    uint8_t error_code;
    int16_t temperature;
    uint8_t calibration_status;
} spoke_system_status_t;

/**
 * @brief Distance mode configuration
 */
typedef struct {
    float start_m;
    float end_m;
    uint8_t max_profile;
    float signal_quality;
    uint8_t threshold_method;
    float threshold_sensitivity;
    uint8_t peak_sorting;
    uint8_t max_num_targets;
} spoke_distance_config_t;

/**
 * @brief Vitals mode configuration
 */
typedef struct {
    float target_distance_m;
    uint8_t measurement_time_s;
    uint8_t hr_min;
    uint8_t hr_max;
    uint8_t rr_min;
    uint8_t rr_max;
    uint8_t hwaas;
    uint8_t profile;
    uint8_t step_length;
} spoke_vitals_config_t;

/* ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize the I2C register protocol
 * @param i2c_handle Pointer to STM32 I2C handle
 * @return true if successful
 */
bool spoke_i2c_protocol_init(void *i2c_handle);

/**
 * @brief Handle I2C register read request
 * @param reg_addr Register address to read
 * @param value Pointer to store read value
 * @return true if successful
 */
bool spoke_i2c_read_register(uint8_t reg_addr, uint8_t *value);

/**
 * @brief Handle I2C register write request
 * @param reg_addr Register address to write
 * @param value Value to write
 * @return true if successful
 */
bool spoke_i2c_write_register(uint8_t reg_addr, uint8_t value);

/**
 * @brief Process pending commands
 * Should be called from main loop
 */
void spoke_i2c_process_commands(void);

/**
 * @brief Update distance results in registers
 * @param result Pointer to distance result structure
 */
void spoke_i2c_update_distance_results(const spoke_distance_result_t *result);

/**
 * @brief Update vitals results in registers
 * @param result Pointer to vitals result structure
 */
void spoke_i2c_update_vitals_results(const spoke_vitals_result_t *result);

/**
 * @brief Get current distance configuration
 * @param config Pointer to store configuration
 */
void spoke_i2c_get_distance_config(spoke_distance_config_t *config);

/**
 * @brief Get current vitals configuration
 * @param config Pointer to store configuration
 */
void spoke_i2c_get_vitals_config(spoke_vitals_config_t *config);

/**
 * @brief Update system status in registers
 * @param status Pointer to system status
 */
void spoke_i2c_update_system_status(const spoke_system_status_t *status);

/**
 * @brief Get current mode from register (set by Hub)
 * @return Current mode value from REG_MODE
 */
uint8_t spoke_i2c_get_current_mode(void);

/**
 * @brief Check if a new command is pending
 * @param cmd Pointer to store command value
 * @return true if command is pending
 */
bool spoke_i2c_get_pending_command(uint8_t *cmd);

/**
 * @brief Clear pending command
 */
void spoke_i2c_clear_command(void);

#endif // SPOKE_I2C_PROTOCOL_H_
