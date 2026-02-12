// imu_driver.h
#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include "BNO08xGlobalTypes.hpp"
#include "BNO08xPrivateTypes.hpp"

/**
 * IMU Driver - Thin wrapper around BNO08x library
 */

// TODO: Replace with FreeRTOS event group bits for cross-core synchronization

/**
 * @brief Configuration for an IMU report, used to enable/disablemultiple reports
 * @param report_id: the ID of the report to enable
 * @param period_us: the period in microseconds to sample the report auto set to 100ms if not provided
 * @param config: the configuration for the report
 */
typedef struct imu_report_cfg_t {
    sh2_SensorId_t report_id;
    uint32_t period_us;
    sh2_SensorConfig_t config = BNO08xPrivateTypes::default_sensor_cfg;
} imu_report_cfg_t;


bool imu_init();


/** 
*     SETTERS/ENABLE FOR ALL REPORTS
* ======================================
* @note Only raw reports have timestamps
* ======================================
*/


/**
* @brief Enable a specific report
* @param report_id: the ID of the report to enable
* @param period_us: the period in microseconds to sample the report auto set to 100ms if not provided
* @param config: the configuration for the report
* @return true if the report was enabled successfully, false otherwise
*/
bool imu_enable_rpt(sh2_SensorId_t report_id, uint32_t period_us = 100000UL,
                       sh2_SensorConfig_t config = BNO08xPrivateTypes::default_sensor_cfg);

bool imu_enable_multi_rpts(imu_report_cfg_t *rpts, size_t count);

/** 
* @brief Disable a specific report
* @param report_id: the ID of the report to disable
* @return true if the report was enabled successfully, false otherwise
*/
bool imu_disable_rpt(sh2_SensorId_t report_id);

bool imu_disable_rpts(imu_report_cfg_t *rpts, size_t count);

/**
* @brief Disable all reports
* @return true if the reports were disabled successfully, false otherwise
*/
bool imu_disable_all_rpts();

/** 
* @brief Check if new data is available for a specific report
* @param report_id: the ID of the report to check
* @return true if new data is available, false otherwise
*/
bool imu_has_new_data(sh2_SensorId_t report_id);

/** 
* @brief Rearm the significant motion report
* @param report_id: the ID of the report to disable
* @param period_us: the period in microseconds to sample the report auto set to 100ms if not provided
* @param config: the configuration for the report
* @return true if the report was enabled successfully, false otherwise
*/
bool imu_rearm_sig_motion(uint32_t period_us = 100000UL, 
                          sh2_SensorConfig_t config = BNO08xPrivateTypes::default_sensor_cfg);



/** 
* ====================================== 
*       GETTERS FOR ALL REPORTS 
* ======================================
* @note Only raw reports have timestamps
* ======================================
*/

/**
* @brief Get the raw accelerometer data
* @return raw accel data struct 
*/
bno08x_raw_accel_t imu_get_raw_accel();

/** 
* @brief Get the linear acceleration data
* @return accel data struct 
*/
bno08x_accel_t imu_get_linear_accel();

/** 
* @brief Get the accelerometer data
* @return accel data struct 
*/
bno08x_accel_t imu_get_accel();

/** 
* @brief Get the gravity data
* @return accel data struct 
*/
bno08x_accel_t imu_get_gravity();

/** 
* @brief Get the raw gyro data
* @return raw gyro data struct
*/
bno08x_raw_gyro_t imu_get_raw_gyro();

/** 
* @brief Get the CALIBRATED gyro data in (rad/s) 
* @return the calibrated gyro data struct 
*/
bno08x_gyro_t imu_get_cal_gyro();


/** 
* @brief Get the UNCALIBRATED gyro data in (rad/s) 
* @return the calibrated gyro data struct 
*/
bno08x_gyro_t imu_get_uncal_gyro();

/** 
* @brief Get the gyro bias data in (rad/s) 
* @return the calibrated gyro data struct 
*/
bno08x_gyro_bias_t imu_get_gyro_bias();

/** 
* @brief Get the raw magnetometer data 
* @return raw magnetometer data struct 
*/
bno08x_raw_magf_t imu_get_raw_magf();

/** 
* @brief Get the CALIBRATED magnetometer data 
* @return raw magnetometer data struct 
*/
bno08x_magf_t imu_get_cal_magf();

/** 
* @brief Get the UNCALIBRATED magnetometer data 
* @return raw magnetometer data struct 
*/
bno08x_magf_t imu_get_uncal_magf();

/**
* @brief Get the magnetometer bias data in (uTesla)
* @return magnetometer bias data struct
*/
bno08x_magf_bias_t imu_get_magf_bias();

/**
* @brief Get the rotation vector quaternion
* @return quaternion data struct
*/
bno08x_quat_t imu_get_rv();

/**
* @brief Get the rotation vector as euler angles
* @param degrees: if true, return degrees; if false, return radians
* @return euler angle data struct
*/
bno08x_euler_angle_t imu_get_rv_euler(bool degrees = true);

/**
* @brief Get the geomagnetic rotation vector quaternion
* @return quaternion data struct
*/
bno08x_quat_t imu_get_rv_geomagnetic();

/**
* @brief Get the geomagnetic rotation vector as euler angles
* @param degrees: if true, return degrees; if false, return radians
* @return euler angle data struct
*/
bno08x_euler_angle_t imu_get_rv_geomagnetic_euler(bool degrees = true);

/**
* @brief Get the activity classifier data
* @return activity classifier data struct
*/
bno08x_activity_classifier_t imu_get_activity_classifier();

/**
* @brief Get the stability classifier data
* @return stability classifier data struct
*/
bno08x_stability_classifier_t imu_get_stability_classifier();

/**
* @brief Get the shake detector data
* @return shake detector data struct
*/
bno08x_shake_detector_t imu_get_shake_detector();

/**
* @brief Get the step counter data
* @return step counter data struct
*/
bno08x_step_counter_t imu_get_step_counter();

/**
* @brief Get the significant motion data
* @return significant motion data struct
*/
bno08x_significant_motion_t imu_get_significant_motion();


/** 
* ====================================== 
*       UTILITY FUNCTIONS
* ======================================
*/

int imu_get_int_pin();                       
bool imu_hard_reset();
bool imu_soft_reset();

/**
* @brief Run the dynamic calibration routine with user-friendly instructions
* @return true if the dynamic calibration routine was run successfully
* @note Displays clear instructions for gyro and magnetometer calibration
*/
bool imu_dynamic_calibration();

/** 
* @brief Get the meta data for a specific report, refer to BNO08xGlobalTypes.hpp for meta data struct
* @param report_id: the ID of the report to get the meta data for
* @param meta_data: the meta data struct to fill
* @return true if the meta data was retrieved successfully, false otherwise
*/
bool imu_get_meta_data(sh2_SensorId_t report_id, bno08x_meta_data_t &meta_data);

/**
* @brief Read an FRS (Flash Record System) record from the IM, esp32_bno08x library handles handshake 
* @param frs_id: the FRS record ID to read (see BNO08xFrsID enum)
* @param data: buffer to store the read data (max 16 uint32_t words)
* @param rx_data_sz: on success, will contain the number of words read
* @return true if the FRS record was read successfully, false otherwise
*/
bool imu_frs_read(BNO08xFrsID frs_id, uint32_t (&data)[16], uint16_t &rx_data_sz);

/**
* @brief Write an FRS (Flash Record System) record to the IMU, esp32_bno08x library handles handshake 
* @param frs_id: the FRS record ID to write (see BNO08xFrsID enum)
* @param data: pointer to the data to write
* @param tx_data_sz: number of uint32_t words to write
* @return true if the FRS record was written successfully, false otherwise
*/
bool imu_frs_write(BNO08xFrsID frs_id, uint32_t *data, uint16_t tx_data_sz);

/**
* @brief Dump raw FRS record contents to serial for debugging
* @param frs_id: the FRS record ID to dump
*/
void imu_frs_dump(BNO08xFrsID frs_id);

/** 
* ===========================================
*   SIGNIFICANT MOTION CONFIGURATION via FRS
* ===========================================
*/
typedef struct {
    float accel_threshold_ms2;  ///< Acceleration threshold in m/s² (default: 10.0)
    uint32_t step_threshold;    ///< Number of steps required (default: 5)
} imu_sig_motion_config_t;

/**
* @brief Get significant motion detector configuration
* @param config: struct to fill with current config
* @return true on success
*/
bool imu_get_sig_motion_config(imu_sig_motion_config_t &config);

/**
* @brief Set significant motion detector configuration
* @param config: struct containing new config values
* @return true on success
*/
bool imu_set_sig_motion_config(const imu_sig_motion_config_t &config);

/**
* @brief Print significant motion config to serial in human-readable format
* @param config: config to print
*/
void imu_print_sig_motion_config(const imu_sig_motion_config_t &config);



//TESTING FUNCTIONS

void motion_detection_task(void *pvParameters);
void data_processing_task(void *pvParameters);



#endif /* IMU_DRIVER_H */