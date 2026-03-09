#include <stdio.h>
#include "BNO08x.hpp"
#include "imu_driver.hpp"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "NimBLEDevice.h"
#include "ble_driver.hpp"

static const char *TAG = "MAIN";

extern "C" void app_main(void) {

    ESP_LOGI(TAG, "Starting BLE...");
    bleServer.init();
    bleServer.startAdvertising();
    ESP_LOGI(TAG, "BLE advertising started");

    ESP_LOGI(TAG, "Initializing IMU...");
    bool imu_ready = false;
    if (!imu_init()) {
        ESP_LOGE(TAG, "IMU init failed! Continuing without IMU.");
    } else {
        ESP_LOGI(TAG, "IMU initialized, waiting for authenticated BLE client");
        imu_ready = true;
    }

    bno08x_meta_data_t meta_data;

    struct { sh2_SensorId_t id; const char* name; } sensors_to_query[] = {
        { SH2_PERSONAL_ACTIVITY_CLASSIFIER, "Activity Classifier" },
        { SH2_STEP_COUNTER,                 "Step Counter"        },
        { SH2_GYROSCOPE_CALIBRATED,         "Cal Gyro"            },
        { SH2_ACCELEROMETER,                "Accelerometer"       },
        { SH2_MAGNETIC_FIELD_CALIBRATED,    "Cal Magnetometer"    },
        { SH2_ROTATION_VECTOR,              "Rotation Vector"     },
        { SH2_GAME_ROTATION_VECTOR,         "Game RV"             },
        { SH2_LINEAR_ACCELERATION,          "Linear Accel"        },
        { SH2_GRAVITY,                      "Gravity"             },
        { SH2_STABILITY_CLASSIFIER,         "Stability Classifier"},
        { SH2_SHAKE_DETECTOR,               "Shake Detector"      },
        { SH2_SIGNIFICANT_MOTION,           "Significant Motion"  },
    };

    for (auto& s : sensors_to_query) {
        if (imu_get_meta_data(s.id, meta_data)) {
            ESP_LOGI(TAG, "%-22s  min: %7lu us  max: %10lu us", s.name, meta_data.min_period_us, meta_data.max_period_us);
        } else {
            ESP_LOGW(TAG, "%-22s  metadata query failed", s.name);
        }
    }

    bool data_task_started = false;

    while (1) {
        Timestamp_t ts = getUTCTimestamp();
        ESP_LOGI(TAG, "Timestamp: %d-%02d-%02d %02d:%02d:%02d.%03d",
            ts.year, ts.month, ts.day,
            ts.hours, ts.minutes, ts.seconds, ts.m_seconds);

        if(imu_ready && !data_task_started && bleServer.isAuthenticated()) {
            ESP_LOGI(TAG, "BLE client authenticated - starting data processing task");
            xTaskCreate(data_processing_task, "imu_task", 4096, NULL, 5, NULL);
            data_task_started = true;
        }

        if(!bleServer.hasSubscriber() && !bleServer.isAdvertising()) {
            ESP_LOGW(TAG, "No client connected and not advertising - restarting advertising");
            if(!bleServer.startAdvertising()) {
                ESP_LOGE(TAG, "Advertising restart failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}