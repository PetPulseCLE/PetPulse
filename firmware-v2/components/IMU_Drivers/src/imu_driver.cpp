#include "BNO08x.hpp"
#include "BNO08xGlobalTypes.hpp"
#include "bsp.h"
#include "imu_driver.hpp"
#include "freertos/task.h"
#include "portmacro.h"
#include "sh2.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ble_driver.h"
#include "rtc_driver.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/time.h>
#include <string.h>
#include <climits>


static constexpr const char *TAG = "IMU_DRIVER";
volatile bool motion_flag = false;

static BNO08x imu;

EventGroupHandle_t imuEventGroup = nullptr;

static int8_t cached_rssi = -127;

// ============================================================================
// Daily step persistence state (NVS-backed)
// ============================================================================
static constexpr const char *NVS_NS_IMU = "imu";
static constexpr uint32_t STEP_FLUSH_THRESHOLD   = 100;          // Flush every N new steps
static constexpr uint64_t STEP_FLUSH_INTERVAL_US  = 60000000ULL; // Flush every 60 seconds

static uint32_t s_daily_steps       = 0;     // RAM mirror of NVS daily count
static uint16_t s_prev_raw_steps    = 0;     // Last BNO08x raw value (for delta)
static bool     s_step_baseline_set = false;  // Whether baseline established this session
static uint32_t s_steps_since_flush = 0;     // Steps accumulated since last NVS write
static uint64_t s_last_flush_us     = 0;     // esp_timer_get_time() at last NVS write

static uint16_t s_stored_year  = 0;          // RAM mirror of NVS date
static uint8_t  s_stored_month = 0;
static uint8_t  s_stored_day   = 0;


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
    return BSP_INT_IMU;
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
uint32_t imu_get_daily_steps(void) { return s_daily_steps; }
bno08x_significant_motion_t imu_get_significant_motion() { return imu.rpt.significant_motion.get();}



// ============================================================================
// IMU data task — constants, helpers, statics
// ============================================================================

#define IMU_HW_PERIOD_US  100000UL   // 10Hz hardware rate

void imu_events_init(void) {
    imuEventGroup = xEventGroupCreate();
}

// ============================================================================
// BNO08x → BLE type conversion helpers
// ============================================================================

static ble_accel_t bno08x_to_ble_accel(const bno08x_accel_t &data) {
    ble_accel_t ble = {};
    ble.x = data.x;
    ble.y = data.y;
    ble.z = data.z;
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}

static ble_gyro_t bno08x_to_ble_gyro(const bno08x_gyro_t &data) {
    ble_gyro_t ble = {};
    ble.x = data.x;
    ble.y = data.y;
    ble.z = data.z;
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}

static ble_magf_t bno08x_to_ble_magf(const bno08x_magf_t &data) {
    ble_magf_t ble = {};
    ble.x = data.x;
    ble.y = data.y;
    ble.z = data.z;
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}

static ble_rv_t bno08x_to_ble_rv(const bno08x_quat_t &data) {
    ble_rv_t ble = {};
    ble.real = data.real;
    ble.x = data.i;
    ble.y = data.j;
    ble.z = data.k;
    ble.rad_accuracy = data.rad_accuracy;
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}

static ble_step_count_t bno08x_to_ble_step(const bno08x_step_counter_t &data) {
    ble_step_count_t ble = {};
    ble.latency  = data.latency;
    ble.steps    = (s_daily_steps > UINT16_MAX) ? UINT16_MAX : (uint16_t)s_daily_steps;
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}

static ble_activity_class_t bno08x_to_ble_activity(const bno08x_activity_classifier_t &data) {
    ble_activity_class_t ble = {};
    memcpy(ble.confidence, data.confidence, sizeof(ble.confidence));
    ble.mostLikelyState = static_cast<uint8_t>(data.mostLikelyState);
    ble.accuracy = static_cast<uint8_t>(data.accuracy);
    return ble;
}


static uint64_t mode_to_period_us(ble_mode_t m) {
    switch (m) {
        case BLE_MODE_BACKGROUND: return 10000000UL;  // 10s
        case BLE_MODE_LIVE:       return  5000000UL;  //  5s
        case BLE_MODE_DEV:        return  1000000UL;  //  1s
        default:                  return 10000000UL;
    }
}

#define IMU_BLE_FEASIBLE() (ble_driver_is_authenticated() && cached_rssi >= -80)

// TODO: replace with real sd_driver implementation
static esp_err_t sd_write_aggregated_record(const ble_aggregated_all_t *record) {
    (void)record;
    return ESP_OK;
}


// ============================================================================
// Daily step persistence helpers
// ============================================================================

static void step_nvs_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS_IMU, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS step load: no data yet (%s), starting from 0", esp_err_to_name(err));
        s_daily_steps  = 0;
        s_stored_year  = 0;
        s_stored_month = 0;
        s_stored_day   = 0;
        return;
    }

    nvs_get_u32(nvs, "d_steps", &s_daily_steps);
    nvs_get_u16(nvs, "d_year",  &s_stored_year);
    nvs_get_u8(nvs,  "d_month", &s_stored_month);
    nvs_get_u8(nvs,  "d_day",   &s_stored_day);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Loaded daily steps: %lu for date %u-%02u-%02u",
             (unsigned long)s_daily_steps, s_stored_year, s_stored_month, s_stored_day);
}

static void step_nvs_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS_IMU, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS step save open failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u32(nvs, "d_steps", s_daily_steps);
    nvs_set_u16(nvs, "d_year",  s_stored_year);
    nvs_set_u8(nvs,  "d_month", s_stored_month);
    nvs_set_u8(nvs,  "d_day",   s_stored_day);

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS step commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(nvs);

    s_steps_since_flush = 0;
    s_last_flush_us = esp_timer_get_time();
}

static void step_check_day_rollover(void)
{
    if (!rtc_is_time_set()) return;

    rtc_datetime_t dt;
    if (rtc_get_datetime(&dt) != ESP_OK) return;

    // First boot: adopt today's date without resetting
    if (s_stored_year == 0) {
        s_stored_year  = dt.year;
        s_stored_month = dt.month;
        s_stored_day   = dt.day;
        return;
    }

    if (dt.year != s_stored_year || dt.month != s_stored_month || dt.day != s_stored_day) {
        ESP_LOGI(TAG, "Day rollover: %u-%02u-%02u -> %u-%02u-%02u (was %lu steps)",
                 s_stored_year, s_stored_month, s_stored_day,
                 dt.year, dt.month, dt.day, (unsigned long)s_daily_steps);
        s_daily_steps  = 0;
        s_stored_year  = dt.year;
        s_stored_month = dt.month;
        s_stored_day   = dt.day;
        step_nvs_save();
    }
}

static void step_accumulate(uint16_t raw_steps)
{
    if (!s_step_baseline_set) {
        s_prev_raw_steps = raw_steps;
        s_step_baseline_set = true;
        return;
    }

    // Unsigned subtraction handles uint16_t rollover naturally
    uint16_t delta = (uint16_t)(raw_steps - s_prev_raw_steps);
    s_prev_raw_steps = raw_steps;

    if (delta == 0) return;

    s_daily_steps += delta;
    s_steps_since_flush += delta;
}

extern "C" void imu_flush_daily_steps(void)
{
    if (s_steps_since_flush > 0) {
        step_nvs_save();
        ESP_LOGI(TAG, "Daily steps flushed to NVS: %lu", (unsigned long)s_daily_steps);
    }
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
    if ((now - last_accel_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_accel_us = now;

    bno08x_accel_t data = imu.rpt.accelerometer.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    ble_accel_t ble = bno08x_to_ble_accel(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_accel(&ble);
        ble_driver_notify_raw(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_RAW_PRESENT_BIT;
        rec.raw.accel = ble;
        rtc_get_datetime_utc(&rec.raw.timestamp);
        sd_write_aggregated_record(&rec);
    }
}

static uint64_t last_gyro_us     = 0;

static void imu_gyro_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_gyro_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_gyro_us = now;

    bno08x_gyro_t data = imu.rpt.cal_gyro.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    ble_gyro_t ble = bno08x_to_ble_gyro(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_gyro(&ble);
        ble_driver_notify_raw(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_RAW_PRESENT_BIT;
        rec.raw.gyro = ble;
        rtc_get_datetime_utc(&rec.raw.timestamp);
        sd_write_aggregated_record(&rec);
    }
}

static uint64_t last_magf_us     = 0;

static void imu_magf_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_magf_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_magf_us = now;

    bno08x_magf_t data = imu.rpt.cal_magnetometer.get();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    ble_magf_t ble = bno08x_to_ble_magf(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_magf(&ble);
        ble_driver_notify_raw(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_RAW_PRESENT_BIT;
        rec.raw.magf = ble;
        rtc_get_datetime_utc(&rec.raw.timestamp);
        sd_write_aggregated_record(&rec);
    }
}

static uint64_t last_rv_us       = 0;

static void imu_rv_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_rv_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_rv_us = now;

    bno08x_quat_t data = imu.rpt.rv.get_quat();
    if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

    ble_rv_t ble = bno08x_to_ble_rv(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_rv(&ble);
        ble_driver_notify_raw(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_RAW_PRESENT_BIT;
        rec.raw.rv = ble;
        rtc_get_datetime_utc(&rec.raw.timestamp);
        sd_write_aggregated_record(&rec);
    }
}

static uint64_t last_step_us     = 0;

static void imu_step_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_step_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_step_us = now;

    bno08x_step_counter_t data = imu.rpt.step_counter.get();

    // Daily step persistence: check day boundary, accumulate delta, periodic flush
    step_check_day_rollover();
    step_accumulate(data.steps);

    if (s_steps_since_flush >= STEP_FLUSH_THRESHOLD ||
        (s_steps_since_flush > 0 && (now - s_last_flush_us) >= STEP_FLUSH_INTERVAL_US)) {
        step_nvs_save();
    }

    ble_step_count_t ble = bno08x_to_ble_step(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_step_count(&ble);
        ble_driver_notify_activity(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_ACTIVITY_PRESENT_BIT;
        rec.activity.stepCount = ble;
        rtc_get_datetime_utc(&rec.activity.timestamp);
        sd_write_aggregated_record(&rec);
    }
}

static uint64_t last_activity_us = 0;

static void imu_activity_cb() {
    /* Gate sending data based on mode */
    uint64_t now = esp_timer_get_time();
    if ((now - last_activity_us) < mode_to_period_us(ble_driver_get_mode())) return;
    last_activity_us = now;

    bno08x_activity_classifier_t data = imu.rpt.activity_classifier.get();

    ble_activity_class_t ble = bno08x_to_ble_activity(data);
    if (IMU_BLE_FEASIBLE()) {
        ble_driver_set_activity_class(&ble);
        ble_driver_notify_activity(true);
    } else {
        ble_aggregated_all_t rec = {};
        rec.presence_bitmask = BLE_AGG_ACTIVITY_PRESENT_BIT;
        rec.activity.activityClass = ble;
        rtc_get_datetime_utc(&rec.activity.timestamp);
        sd_write_aggregated_record(&rec);
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

static void imu_register_callbacks(ble_mode_t m) {
    imu.rpt.step_counter.enable(IMU_HW_PERIOD_US);
    imu.rpt.step_counter.register_cb(imu_step_cb);

    imu.rpt.activity_classifier.enable(IMU_HW_PERIOD_US);
    imu.rpt.activity_classifier.register_cb(imu_activity_cb);

    if (m == BLE_MODE_DEV) {
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

    // Load persisted daily step count from NVS
    step_nvs_load();
    s_last_flush_us = esp_timer_get_time();

    /* Wait until BLE is authenticated and RTC has been set via CTS */
    ESP_LOGI(TAG_DATA, "Waiting for BLE auth + time sync...");
    while (!ble_driver_is_authenticated() || !rtc_is_time_set()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG_DATA, "BLE ready — starting IMU data collection");


    /* Wait till we detect sig motion */
    ESP_LOGI(TAG_DATA, "Waiting for motion");
    xEventGroupWaitBits(imuEventGroup, IMU_EVT_MOTION_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG_DATA, "Motion detected");

    ble_mode_t current_mode = ble_driver_get_mode();
    imu_register_callbacks(current_mode);
    last_static_us = esp_timer_get_time();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        cached_rssi = ble_driver_get_rssi();

        ble_mode_t new_mode = ble_driver_get_mode();
        if (new_mode != current_mode) {
            ESP_LOGI(TAG_DATA, "Mode change: %d -> %d", (int)current_mode, (int)new_mode);
            if (new_mode == BLE_MODE_DEV) {
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
                last_accel_us = last_gyro_us = last_magf_us = last_rv_us = last_step_us = last_activity_us = 0;
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
