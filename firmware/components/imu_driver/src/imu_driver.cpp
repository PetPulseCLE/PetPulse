#include "BNO08x.hpp"
#include "BNO08xGlobalTypes.hpp"
#include "imu_driver.hpp"
#include "freertos/task.h"
#include "portmacro.h"
#include "sh2.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ble_driver.hpp"


static constexpr const char *TAG = "IMU_DRIVER";
volatile bool motion_flag = false;

static BNO08x imu;

EventGroupHandle_t imuEventGroup = nullptr;

static int8_t cached_rssi = -127;


bool imu_init() {
    if (!imu.initialize()) {

        ESP_LOGE(TAG, "Init failure, returning from imu_driver.");
        return false;
    }
    // Set init bits to indicate down stream tasks
    xEventGroupSetBits(imuEventGroup, IMU_EVT_INIT_BIT);
    ESP_LOGI(TAG, "IMU - INITIALIZED");
    return true;
}


bool imu_hard_reset() {
    imu.hard_reset();
    ESP_LOGI(TAG, "IMU - HARD RESET");
    return true;
}

bool imu_soft_reset() {
    imu.soft_reset();
    ESP_LOGI(TAG, "IMU - SOFT RESET");
    return true;
}

bool imu_disable_all_rpts() {
    imu.disable_all_reports();
    ESP_LOGI(TAG, "IMU - ALL REPORTS DISABLED");
    return true;
}

int imu_get_int_pin() {
    bno08x_config_t imu_config; 
    return imu_config.io_int;
}

bool imu_dynamic_calibration() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   IMU CALIBRATION ROUTINE STARTED");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "GYROSCOPE CALIBRATION:");
    ESP_LOGI(TAG, "  - Perform slow, smooth rotations around each axis");
    ESP_LOGI(TAG, "  - Rotate X, Y, and Z axes independently");
    ESP_LOGI(TAG, "  - Move slowly and deliberately");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MAGNETOMETER CALIBRATION:");
    ESP_LOGI(TAG, "  - Perform figure-8 motions in the air");
    ESP_LOGI(TAG, "  - OR random tumbling to expose all orientations");
    ESP_LOGI(TAG, "  - Cover as many directions as possible");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Calibration will complete automatically when");
    ESP_LOGI(TAG, "accuracy thresholds are met (approx. 5 seconds).");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    if(imu.dynamic_calibration_run_routine()) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "   CALIBRATION COMPLETE - SUCCESS!");
        ESP_LOGI(TAG, "========================================");
        return true;
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "   CALIBRATION FAILED");
        ESP_LOGE(TAG, "========================================");
        return false;
    }
}

bool imu_frs_read(BNO08xFrsID frs_id, uint32_t (&data)[16], uint16_t &rx_data_sz) {
    if (!imu.get_frs(frs_id, data, rx_data_sz)) {
        ESP_LOGE(TAG, "Failed to read FRS record: %s (0x%04X)",
                 BNO08xFrsID_to_str(frs_id), static_cast<uint16_t>(frs_id));
        return false;
    }
    ESP_LOGI(TAG, "FRS read success: %s, %u words", BNO08xFrsID_to_str(frs_id), rx_data_sz);
    return true;
}

bool imu_frs_write(BNO08xFrsID frs_id, uint32_t *data, uint16_t tx_data_sz) {
    if (data == nullptr || tx_data_sz == 0) {
        ESP_LOGE(TAG, "Invalid FRS write parameters");
        return false;
    }

    if (!imu.write_frs(frs_id, data, tx_data_sz)) {
        ESP_LOGE(TAG, "Failed to write FRS record: %s (0x%04X)",
                 BNO08xFrsID_to_str(frs_id), static_cast<uint16_t>(frs_id));
        return false;
    }
    ESP_LOGI(TAG, "FRS write success: %s, %u words", BNO08xFrsID_to_str(frs_id), tx_data_sz);
    return true;
}

void imu_frs_dump(BNO08xFrsID frs_id) {
    uint32_t data[16] = {0};
    uint16_t size = 0;

    if (!imu_frs_read(frs_id, data, size)) {
        ESP_LOGE(TAG, "FRS dump failed for %s", BNO08xFrsID_to_str(frs_id));
        return;
    }

    ESP_LOGI(TAG, "=== FRS Dump: %s (0x%04X) ===",
             BNO08xFrsID_to_str(frs_id), static_cast<uint16_t>(frs_id));
    ESP_LOGI(TAG, "Size: %u words", size);
    for (uint16_t i = 0; i < size; i++) {
        ESP_LOGI(TAG, "  Word[%u]: 0x%08lX (%lu)", i, data[i], data[i]);
    }
    ESP_LOGI(TAG, "=== End FRS Dump ===");
}

// ============================================================================
// Significant Motion Detector Configuration (FRS 0xC274)
// Q24 = 16777216, Q26 = 67108864
// ============================================================================
static constexpr float Q24_SCALE = 16777216.0f;
static constexpr float Q26_SCALE = 67108864.0f;

bool imu_get_sig_motion_config(imu_sig_motion_config_t &config) {
    uint32_t data[16] = {0};
    uint16_t size = 0;

    if (!imu_frs_read(BNO08xFrsID::SIG_MOTION_DETECT_CONFIG, data, size)) {
        return false;
    }

    if (size < 2) {
        ESP_LOGE(TAG, "Sig motion config too small: %u words", size);
        return false;
    }

    // Word 0: Acceleration threshold (Q24 signed fixed point)
    config.accel_threshold_ms2 = static_cast<int32_t>(data[0]) / Q24_SCALE;
    // Word 1: Step threshold (unsigned integer)
    config.step_threshold = data[1];

    return true;
}

bool imu_set_sig_motion_config(const imu_sig_motion_config_t &config) {
    uint32_t data[2];

    // Word 0: Acceleration threshold (Q24)
    data[0] = static_cast<uint32_t>(static_cast<int32_t>(config.accel_threshold_ms2 * Q24_SCALE));
    // Word 1: Step threshold
    data[1] = config.step_threshold;

    return imu_frs_write(BNO08xFrsID::SIG_MOTION_DETECT_CONFIG, data, 2);
}

void imu_print_sig_motion_config(const imu_sig_motion_config_t &config) {
    ESP_LOGI(TAG, "=== Significant Motion Config ===");
    ESP_LOGI(TAG, "  Accel Threshold: %.2f m/s^2", config.accel_threshold_ms2);
    ESP_LOGI(TAG, "  Step Threshold:  %lu steps", config.step_threshold);
    ESP_LOGI(TAG, "=================================");
}


bool imu_get_meta_data(sh2_SensorId_t report_id, bno08x_meta_data_t &meta_data) {
    switch (report_id) {
        case SH2_RAW_ACCELEROMETER:
            return imu.rpt.raw_accelerometer.get_meta_data(meta_data);
        case SH2_ACCELEROMETER:
            return imu.rpt.accelerometer.get_meta_data(meta_data);

        case SH2_GYROSCOPE_CALIBRATED:
            return imu.rpt.cal_gyro.get_meta_data(meta_data);

        case SH2_GYROSCOPE_UNCALIBRATED:
            return imu.rpt.uncal_gyro.get_meta_data(meta_data);

        case SH2_RAW_MAGNETOMETER:
            return imu.rpt.raw_magnetometer.get_meta_data(meta_data);

        case SH2_MAGNETIC_FIELD_CALIBRATED:
            return imu.rpt.cal_magnetometer.get_meta_data(meta_data);

        case SH2_MAGNETIC_FIELD_UNCALIBRATED:
            return imu.rpt.uncal_magnetometer.get_meta_data(meta_data);

        case SH2_ROTATION_VECTOR:
            return imu.rpt.rv.get_meta_data(meta_data);

        case SH2_GAME_ROTATION_VECTOR:
            return imu.rpt.rv_game.get_meta_data(meta_data);

        case SH2_ARVR_STABILIZED_RV:
            return imu.rpt.rv_ARVR_stabilized.get_meta_data(meta_data);

        case SH2_ARVR_STABILIZED_GRV:
            return imu.rpt.rv_ARVR_stabilized_game.get_meta_data(meta_data);

        case SH2_GYRO_INTEGRATED_RV:
            return imu.rpt.rv_gyro_integrated.get_meta_data(meta_data);

        case SH2_GEOMAGNETIC_ROTATION_VECTOR:
            return imu.rpt.rv_geomagnetic.get_meta_data(meta_data);

        case SH2_PERSONAL_ACTIVITY_CLASSIFIER:
            return imu.rpt.activity_classifier.get_meta_data(meta_data);

        case SH2_STABILITY_CLASSIFIER:
            return imu.rpt.stability_classifier.get_meta_data(meta_data);

        case SH2_SHAKE_DETECTOR:
            return imu.rpt.shake_detector.get_meta_data(meta_data);

        case SH2_STEP_COUNTER:
            return imu.rpt.step_counter.get_meta_data(meta_data);

        case SH2_SIGNIFICANT_MOTION:
            return imu.rpt.significant_motion.get_meta_data(meta_data);

        default:
            ESP_LOGE(TAG, "Invalid report ID: %d", report_id);
            return false;
    }
    return true;
}


bool imu_enable_rpt(sh2_SensorId_t report_id, uint32_t period_us, sh2_SensorConfig_t config) {
    switch (report_id) {
        case SH2_RAW_ACCELEROMETER:
            return imu.rpt.raw_accelerometer.enable(period_us, config);

        case SH2_ACCELEROMETER:
            return imu.rpt.accelerometer.enable(period_us, config);

        case SH2_LINEAR_ACCELERATION:
            return imu.rpt.linear_accelerometer.enable(period_us, config);

        case SH2_GRAVITY:
            return imu.rpt.gravity.enable(period_us, config);

        case SH2_RAW_GYROSCOPE:
            return imu.rpt.raw_gyro.enable(period_us, config);

        case SH2_GYROSCOPE_CALIBRATED:
            return imu.rpt.cal_gyro.enable(period_us, config);

        case SH2_GYROSCOPE_UNCALIBRATED:
            return imu.rpt.uncal_gyro.enable(period_us, config);

        case SH2_RAW_MAGNETOMETER:
            return imu.rpt.raw_magnetometer.enable(period_us, config);

        case SH2_MAGNETIC_FIELD_CALIBRATED:
            return imu.rpt.cal_magnetometer.enable(period_us, config);

        case SH2_MAGNETIC_FIELD_UNCALIBRATED:
            return imu.rpt.uncal_magnetometer.enable(period_us, config);

        case SH2_ROTATION_VECTOR:
            return imu.rpt.rv.enable(period_us, config);

        case SH2_GAME_ROTATION_VECTOR:
            return imu.rpt.rv_game.enable(period_us, config);

        case SH2_ARVR_STABILIZED_RV:
            return imu.rpt.rv_ARVR_stabilized.enable(period_us, config);

        case SH2_ARVR_STABILIZED_GRV:
            return imu.rpt.rv_ARVR_stabilized_game.enable(period_us, config);

        case SH2_GYRO_INTEGRATED_RV:
            return imu.rpt.rv_gyro_integrated.enable(period_us, config);

        case SH2_GEOMAGNETIC_ROTATION_VECTOR:
            return imu.rpt.rv_geomagnetic.enable(period_us, config);

        case SH2_PERSONAL_ACTIVITY_CLASSIFIER:
            return imu.rpt.activity_classifier.enable(period_us, config);

        case SH2_STABILITY_CLASSIFIER:
            return imu.rpt.stability_classifier.enable(period_us, config);

        case SH2_SHAKE_DETECTOR:
            return imu.rpt.shake_detector.enable(period_us, config);

        case SH2_STEP_COUNTER:
            return imu.rpt.step_counter.enable(period_us, config);

        case SH2_SIGNIFICANT_MOTION:
            return imu.rpt.significant_motion.enable(period_us, config);

            
        default:
            ESP_LOGE(TAG, "Invalid report ID: %d", report_id);
            return false;
    }
    return true;
}

bool imu_enable_multi_rpts(imu_report_cfg_t *rpts, size_t count) {
    bool all_enabled = true;
    for(size_t i = 0; i < count; i++) {
        if(!imu_enable_rpt(rpts[i].report_id, rpts[i].period_us, rpts[i].config)) {
            all_enabled = false;
        }
    }
    return all_enabled;
}



bool imu_disable_rpt(sh2_SensorId_t report_id) {
    switch (report_id) {
        case SH2_RAW_ACCELEROMETER:
            return imu.rpt.raw_accelerometer.disable();

        case SH2_ACCELEROMETER:
            return imu.rpt.accelerometer.disable();

        case SH2_LINEAR_ACCELERATION:
            return imu.rpt.linear_accelerometer.disable();

        case SH2_GRAVITY:
            return imu.rpt.gravity.disable();

        case SH2_RAW_GYROSCOPE:
            return imu.rpt.raw_gyro.disable();

        case SH2_GYROSCOPE_CALIBRATED:
            return imu.rpt.cal_gyro.disable();

        case SH2_GYROSCOPE_UNCALIBRATED:
            return imu.rpt.uncal_gyro.disable();

        case SH2_RAW_MAGNETOMETER:
            return imu.rpt.raw_magnetometer.disable();

        case SH2_MAGNETIC_FIELD_CALIBRATED:
            return imu.rpt.cal_magnetometer.disable();

        case SH2_MAGNETIC_FIELD_UNCALIBRATED:
            return imu.rpt.uncal_magnetometer.disable();

        case SH2_ROTATION_VECTOR:
            return imu.rpt.rv.disable();

        case SH2_GAME_ROTATION_VECTOR:
            return imu.rpt.rv_game.disable();

        case SH2_ARVR_STABILIZED_RV:
            return imu.rpt.rv_ARVR_stabilized.disable();

        case SH2_ARVR_STABILIZED_GRV:
            return imu.rpt.rv_ARVR_stabilized_game.disable();

        case SH2_GYRO_INTEGRATED_RV:
            return imu.rpt.rv_gyro_integrated.disable();

        case SH2_GEOMAGNETIC_ROTATION_VECTOR:
            return imu.rpt.rv_geomagnetic.disable();

        case SH2_PERSONAL_ACTIVITY_CLASSIFIER:
            return imu.rpt.activity_classifier.disable();

        case SH2_STABILITY_CLASSIFIER:
            return imu.rpt.stability_classifier.disable();

        case SH2_SHAKE_DETECTOR:
            return imu.rpt.shake_detector.disable();

        case SH2_STEP_COUNTER:
            return imu.rpt.step_counter.disable();

        case SH2_SIGNIFICANT_MOTION:
            return imu.rpt.significant_motion.disable();

            
        default:
            ESP_LOGE(TAG, "Invalid report ID: %d", report_id);
            return false;
    }
    return true;
}

bool imu_disable_rpts(imu_report_cfg_t *rpts, size_t count) {
    bool all_enabled = true;
    for(size_t i = 0; i < count; i++) {
        if(!imu_disable_rpt(rpts[i].report_id)) {
            all_enabled = false;
        }
    }
    return all_enabled;
}


bool imu_has_new_data(sh2_SensorId_t report_id) {
    switch (report_id) {
        case SH2_RAW_ACCELEROMETER:
            return imu.rpt.raw_accelerometer.has_new_data();

        case SH2_ACCELEROMETER:
            return imu.rpt.accelerometer.has_new_data();

        case SH2_LINEAR_ACCELERATION:
            return imu.rpt.linear_accelerometer.has_new_data();

        case SH2_GRAVITY:
            return imu.rpt.gravity.has_new_data();

        case SH2_RAW_GYROSCOPE:
            return imu.rpt.raw_gyro.has_new_data();

        case SH2_GYROSCOPE_CALIBRATED:
            return imu.rpt.cal_gyro.has_new_data();

        case SH2_GYROSCOPE_UNCALIBRATED:
            return imu.rpt.uncal_gyro.has_new_data();

        case SH2_RAW_MAGNETOMETER:
            return imu.rpt.raw_magnetometer.has_new_data();

        case SH2_MAGNETIC_FIELD_CALIBRATED:
            return imu.rpt.cal_magnetometer.has_new_data();

        case SH2_MAGNETIC_FIELD_UNCALIBRATED:
            return imu.rpt.uncal_magnetometer.has_new_data();

        case SH2_ROTATION_VECTOR:
            return imu.rpt.rv.has_new_data();

        case SH2_GAME_ROTATION_VECTOR:
            return imu.rpt.rv_game.has_new_data();

        case SH2_ARVR_STABILIZED_RV:
            return imu.rpt.rv_ARVR_stabilized.has_new_data();

        case SH2_ARVR_STABILIZED_GRV:
            return imu.rpt.rv_ARVR_stabilized_game.has_new_data();

        case SH2_GYRO_INTEGRATED_RV:
            return imu.rpt.rv_gyro_integrated.has_new_data();

        case SH2_GEOMAGNETIC_ROTATION_VECTOR:
            return imu.rpt.rv_geomagnetic.has_new_data();

        case SH2_PERSONAL_ACTIVITY_CLASSIFIER:
            return imu.rpt.activity_classifier.has_new_data();

        case SH2_STABILITY_CLASSIFIER:
            return imu.rpt.stability_classifier.has_new_data();

        case SH2_SHAKE_DETECTOR:
            return imu.rpt.shake_detector.has_new_data();

        case SH2_STEP_COUNTER:
            return imu.rpt.step_counter.has_new_data();

        case SH2_SIGNIFICANT_MOTION:
            return imu.rpt.significant_motion.has_new_data();

        default:
            ESP_LOGE(TAG, "Invalid report ID: %d", report_id);
            return false;
    }
    return true;
}

bno08x_raw_accel_t imu_get_raw_accel() { return imu.rpt.raw_accelerometer.get(); }
bno08x_accel_t imu_get_accel() { return imu.rpt.accelerometer.get(); }
bno08x_accel_t imu_get_linear_accel() { return imu.rpt.linear_accelerometer.get(); }
bno08x_accel_t imu_get_gravity() { return imu.rpt.gravity.get(); }
bno08x_raw_gyro_t imu_get_raw_gyro() { return imu.rpt.raw_gyro.get(); }
bno08x_gyro_t imu_get_cal_gyro() { return imu.rpt.cal_gyro.get(); }
bno08x_gyro_t imu_get_uncal_gyro() { return imu.rpt.uncal_gyro.get_vel(); }
bno08x_gyro_bias_t imu_get_gyro_bias() { return imu.rpt.uncal_gyro.get_bias(); }
bno08x_raw_magf_t imu_get_raw_magf() { return imu.rpt.raw_magnetometer.get(); }
bno08x_magf_t imu_get_cal_magf() { return imu.rpt.cal_magnetometer.get(); }
bno08x_magf_t imu_get_uncal_magf() { return imu.rpt.uncal_magnetometer.get_magf(); }
bno08x_magf_bias_t imu_get_magf_bias() { return imu.rpt.uncal_magnetometer.get_bias(); }
bno08x_quat_t imu_get_rv() { return imu.rpt.rv.get_quat(); }
bno08x_euler_angle_t imu_get_rv_euler(bool degrees) { return imu.rpt.rv.get_euler(degrees);}
bno08x_quat_t imu_get_rv_geomagnetic() { return imu.rpt.rv_geomagnetic.get_quat(); }
bno08x_euler_angle_t imu_get_rv_geomagnetic_euler(bool degrees) { return imu.rpt.rv_geomagnetic.get_euler(degrees); }
bno08x_activity_classifier_t imu_get_activity_classifier() { return imu.rpt.activity_classifier.get(); }
bno08x_stability_classifier_t imu_get_stability_classifier() { return imu.rpt.stability_classifier.get(); }
bno08x_shake_detector_t imu_get_shake_detector() { return imu.rpt.shake_detector.get(); }
bno08x_step_counter_t imu_get_step_counter() { return imu.rpt.step_counter.get(); }
bno08x_significant_motion_t imu_get_significant_motion() { return imu.rpt.significant_motion.get();}



// ============================================================================
// IMU data task — constants, helpers, statics
// ============================================================================

#define IMU_HW_PERIOD_US  100000UL   // 10Hz hardware rate

void imu_events_init(void) {
    imuEventGroup = xEventGroupCreate();
}

static uint64_t mode_to_period_us(Mode m) {
    switch (m) {
        case Background: return 10000000UL;  // 10s
        case Live:       return  5000000UL;  //  5s
        case Dev:        return  1000000UL;  //  1s
        default:         return 10000000UL;
    }
}

#define IMU_BLE_FEASIBLE() (bleServer.isAuthenticated() && cached_rssi >= -80)

// TODO: replace with real sd_driver implementation
// NOTE: requires BleAggregatedAll_t at file scope (ble_driver Task 1)
static esp_err_t sd_write_aggregated_record(const BleAggregatedAll_t &record) {
    (void)record;
    return ESP_OK;
}


// ============================================================================
// Per-sensor callbacks
// ============================================================================

static void motion_cb() {
    ESP_LOGI("IMU_SIG_MOTION", "Motion detected! Setting MOTION_BIT");
    xEventGroupSetBits(imuEventGroup, IMU_EVT_MOTION_BIT);
}

static uint64_t last_accel_us = 0;

static void imu_accel_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_accel_us) < mode_to_period_us(bleServer.getMode())) return;
    last_accel_us = now;

    bno08x_accel_t data = imu.rpt.accelerometer.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    Timestamp_t ts = getUTCTimestamp();
    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateAccel(data);
        bleServer.setRaw();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_RAW_PRESENT_BIT;
        rec.raw.accel = data;
        rec.raw.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

static uint64_t last_gyro_us     = 0;

static void imu_gyro_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_gyro_us) < mode_to_period_us(bleServer.getMode())) return;
    last_gyro_us = now;

    bno08x_gyro_t data = imu.rpt.cal_gyro.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    Timestamp_t ts = getUTCTimestamp();
    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateGyro(data);
        bleServer.setRaw();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_RAW_PRESENT_BIT;
        rec.raw.gyro = data;
        rec.raw.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

static uint64_t last_magf_us     = 0;

static void imu_magf_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_magf_us) < mode_to_period_us(bleServer.getMode())) return;
    last_magf_us = now;

    bno08x_magf_t data = imu.rpt.cal_magnetometer.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    Timestamp_t ts = getUTCTimestamp();
    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateMagf(data);
        bleServer.setRaw();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_RAW_PRESENT_BIT;
        rec.raw.magf = data;
        rec.raw.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

static uint64_t last_rv_us       = 0;

static void imu_rv_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_rv_us) < mode_to_period_us(bleServer.getMode())) return;
    last_rv_us = now;

    bno08x_quat_t data = imu.rpt.rv.get_quat();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    Timestamp_t ts = getUTCTimestamp();
    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateRV(data);
        bleServer.setRaw();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_RAW_PRESENT_BIT;
        rec.raw.rv = data;
        rec.raw.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

static uint64_t last_step_us     = 0;

static void imu_step_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_step_us) < mode_to_period_us(bleServer.getMode())) return;
    last_step_us = now;

    bno08x_step_counter_t data = imu.rpt.step_counter.get();
    Timestamp_t ts = getUTCTimestamp();

    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateStepCount(data);
        bleServer.setActivity();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_ACTIVITY_PRESENT_BIT;
        rec.activity.stepCount = data;
        rec.activity.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

static uint64_t last_activity_us = 0;

static void imu_activity_cb() {
    /* Gate sending data based on mode */
    uint64_t now = esp_timer_get_time();
    if ((now - last_activity_us) < mode_to_period_us(bleServer.getMode())) return;
    last_activity_us = now;
    
    bno08x_activity_classifier_t data = imu.rpt.activity_classifier.get();
    Timestamp_t ts = getUTCTimestamp();

    if (IMU_BLE_FEASIBLE()) {
        bleServer.updateActivityClass(data);
        bleServer.setActivity();
    } else {
        BleAggregatedAll_t rec = {};
        rec.presence_bitmask = AGG_ACTIVITY_PRESENT_BIT;
        rec.activity.activityClass = data;
        rec.activity.timestamp = ts;
        sd_write_aggregated_record(rec);
    }
}

bool imu_rearm_sig_motion(uint32_t period_us, sh2_SensorConfig_t config) {

    if(imu.rpt.significant_motion.has_new_data()) {
        imu.rpt.significant_motion.flush();
    }

    if(!imu.rpt.significant_motion.disable()) {
        ESP_LOGE(TAG, "IMU - FAILED TO DISABLE SIGNIFICANT MOTION REPORT");
        return false;
    }

    if(!imu.rpt.significant_motion.enable(period_us, config)) {
        ESP_LOGE(TAG, "IMU - FAILED TO ENABLE SIGNIFICANT MOTION REPORT");
        return false;
    }

    ESP_LOGI(TAG, "IMU - SIGNIFICANT MOTION REPORT REARMED");
    return true;
}



// ============================================================================
// Callback registration helper
// ============================================================================

static void imu_register_callbacks(Mode m) {
    imu.rpt.step_counter.enable(IMU_HW_PERIOD_US);
    imu.rpt.step_counter.register_cb(imu_step_cb);

    imu.rpt.activity_classifier.enable(IMU_HW_PERIOD_US);
    imu.rpt.activity_classifier.register_cb(imu_activity_cb);

    if (m == Dev) {
        imu.rpt.accelerometer.enable(IMU_HW_PERIOD_US);
        imu.rpt.accelerometer.register_cb(imu_accel_cb);

        imu.rpt.cal_gyro.enable(IMU_HW_PERIOD_US);
        imu.rpt.cal_gyro.register_cb(imu_gyro_cb);

        imu.rpt.cal_magnetometer.enable(IMU_HW_PERIOD_US);
        imu.rpt.cal_magnetometer.register_cb(imu_magf_cb);

        imu.rpt.rv.enable(IMU_HW_PERIOD_US);
        imu.rpt.rv.register_cb(imu_rv_cb);
    }
}

// ============================================================================
// IMU data task
// ============================================================================

static int64_t last_static_us = 0;

void imu_data_task(void *pv) {
    static const char *TAG_DATA = "IMU_Data";

    if (!imu_init()) {
        ESP_LOGE(TAG_DATA, "imu_init() failed");
        vTaskDelete(NULL);
        return;
    }

    /* Enable when ble connected and time sync was sent*/
    ESP_LOGI(TAG_DATA, "Waiting for BLE auth + time sync...");
    xEventGroupWaitBits(bleEventGroup,
        BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);
    ESP_LOGI(TAG_DATA, "BLE ready — starting IMU data collection");


    /* Wait till we detect sig motion */
    ESP_LOGI(TAG_DATA, "Waiting for motion");
    xEventGroupWaitBits(imuEventGroup, IMU_EVT_MOTION_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG_DATA, "Motion detected");

    Mode current_mode = bleServer.getMode();
    imu_register_callbacks(current_mode);
    last_static_us = esp_timer_get_time();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        cached_rssi = bleServer.getRSSI();

        Mode new_mode = bleServer.getMode();
        if (new_mode != current_mode) {
            ESP_LOGI(TAG_DATA, "Mode change: %d -> %d", (int)current_mode, (int)new_mode);
            if (new_mode == Dev) {
                imu.rpt.accelerometer.enable(IMU_HW_PERIOD_US);
                imu.rpt.accelerometer.register_cb(imu_accel_cb);
                imu.rpt.cal_gyro.enable(IMU_HW_PERIOD_US);
                imu.rpt.cal_gyro.register_cb(imu_gyro_cb);
                imu.rpt.cal_magnetometer.enable(IMU_HW_PERIOD_US);
                imu.rpt.cal_magnetometer.register_cb(imu_magf_cb);
                imu.rpt.rv.enable(IMU_HW_PERIOD_US);
                imu.rpt.rv.register_cb(imu_rv_cb);
            } else {
                imu.rpt.accelerometer.disable();
                imu.rpt.cal_gyro.disable();
                imu.rpt.cal_magnetometer.disable();
                imu.rpt.rv.disable();
            }
            current_mode = new_mode;
        }

        if(imu.rpt.activity_classifier.get().mostLikelyState == BNO08xActivity::STILL) {
            if(esp_timer_get_time() - last_static_us > 300000000UL) { // 5 minutes
                ESP_LOGI(TAG_DATA, "Static state detected, suspending data collection");
                
                imu_disable_all_rpts();

                xEventGroupSetBits(imuEventGroup, IMU_EVT_STATIC_BIT);
                xEventGroupClearBits(imuEventGroup, IMU_EVT_MOTION_BIT);
                
                ESP_LOGI(TAG_DATA, "Waiting for motion");
                xEventGroupWaitBits(imuEventGroup, IMU_EVT_MOTION_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
                ESP_LOGI(TAG_DATA, "Motion detected, resuming data collection");
                
                last_static_us = esp_timer_get_time();
                imu_register_callbacks(current_mode);
            }
        } else {
            last_static_us = esp_timer_get_time();
        }
    }
}

void imu_motion_task(void *pv) {
    static const char *TAG_MOTION = "IMU_Motion";

    xEventGroupWaitBits(imuEventGroup, IMU_EVT_INIT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    if (!imu.rpt.significant_motion.enable(IMU_HW_PERIOD_US)) { 
        ESP_LOGE(TAG_MOTION, "Failed to enable significant motion!");
    }

    imu.rpt.significant_motion.register_cb(motion_cb);
    ESP_LOGI(TAG_MOTION, "Significant motion armed");

    while (true) {
        // Wait for static bit to rearm significant motion
        xEventGroupWaitBits(imuEventGroup, IMU_EVT_STATIC_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG_MOTION, "Static state detected, rearming significant motion");
        imu_rearm_sig_motion();
        imu.rpt.significant_motion.register_cb(motion_cb);
    }
}
