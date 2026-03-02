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
    bleServer.init(9);
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