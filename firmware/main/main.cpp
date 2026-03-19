#include <stdio.h>
#include "BNO08x.hpp"
#include "freertos/idf_additions.h"
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

    imu_events_init();

    TaskHandle_t data_task_handle = NULL;
    BaseType_t data_ret = xTaskCreatePinnedToCore(
        imu_data_task, "imu_data", 8192, NULL, 4, &data_task_handle, 1);
    if (data_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create imu_data_task");
    }

    TaskHandle_t motion_task_handle = NULL;
    BaseType_t motion_ret = xTaskCreatePinnedToCore(
        imu_motion_task, "imu_motion", 4096, &data_task_handle, 3, &motion_task_handle, 1);
    if (motion_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create imu_motion_task");
    }


    while (true) {
        if (!bleServer.hasSubscriber() && !bleServer.isAdvertising()) {
            ESP_LOGW(TAG, "No client connected and not advertising - restarting advertising");
            if (!bleServer.startAdvertising()) {
                ESP_LOGE(TAG, "Advertising restart failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}