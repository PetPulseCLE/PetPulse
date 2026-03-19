#include "sht4x.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SHT4x"

// SHT4x Commands
#define CMD_READ_SERIAL 0x89
#define CMD_SOFT_RESET  0x94

// CRC-8 Parameters (Polynomial 0x31, Init 0xFF)
static uint8_t sht4x_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Helper to send a single-byte command
static esp_err_t sht4x_send_cmd(uint8_t cmd) {
    return i2c_master_write_to_device(SHT40_I2C_PORT, SHT40_ADDRESS, &cmd, 1, pdMS_TO_TICKS(100));
}

// Helper to convert raw data
static void sht4x_convert_data(uint8_t *buffer, sht4x_data_t *data) {
    uint16_t t_ticks = (buffer[0] << 8) | buffer[1];
    uint16_t rh_ticks = (buffer[3] << 8) | buffer[4];

    // Temperature conversion: -45 + 175 * t_ticks / 65535
    data->temperature = -45.0f + 175.0f * ((float)t_ticks / 65535.0f);

    // Humidity conversion: -6 + 125 * rh_ticks / 65535
    data->humidity = -6.0f + 125.0f * ((float)rh_ticks / 65535.0f);

    // Clamp Humidity
    if (data->humidity < 0.0f) data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;
}

esp_err_t sht4x_init(sht4x_data_t *handle_data) {
    esp_err_t ret = sht4x_soft_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft Reset Failed");
        return ret;
    }

    // Wait for reset to complete
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read Serial Number
    ret = sht4x_send_cmd(CMD_READ_SERIAL);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t buffer[6];
    ret = i2c_master_read_from_device(SHT40_I2C_PORT, SHT40_ADDRESS, buffer, 6, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;

    // Verify CRCs
    if (sht4x_crc8(&buffer[0], 2) != buffer[2] || sht4x_crc8(&buffer[3], 2) != buffer[5]) {
        ESP_LOGE(TAG, "Serial CRC Error");
        return ESP_ERR_INVALID_CRC;
    }

    uint32_t serial = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) | ((uint32_t)buffer[3] << 8) | buffer[4];
    if (handle_data) {
        handle_data->serial_number = serial;
    }

    ESP_LOGI(TAG, "Initialized. Serial: 0x%08lX", serial);
    return ESP_OK;
}

esp_err_t sht4x_measure(sht4x_precision_t precision, sht4x_data_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = sht4x_send_cmd(precision);
    if (ret != ESP_OK) return ret;

    // Wait time depends on precision
    TickType_t wait_ticks;
    switch (precision) {
        case SHT4X_HIGH_PRECISION: wait_ticks = pdMS_TO_TICKS(10); break;
        case SHT4X_MEDIUM_PRECISION: wait_ticks = pdMS_TO_TICKS(6); break;
        case SHT4X_LOW_PRECISION: wait_ticks = pdMS_TO_TICKS(3); break;
        default: wait_ticks = pdMS_TO_TICKS(10); break;
    }
    vTaskDelay(wait_ticks);

    uint8_t buffer[6];
    ret = i2c_master_read_from_device(SHT40_I2C_PORT, SHT40_ADDRESS, buffer, 6, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;

    // Verify CRCs
    if (sht4x_crc8(&buffer[0], 2) != buffer[2] || sht4x_crc8(&buffer[3], 2) != buffer[5]) {
        ESP_LOGE(TAG, "Measurement CRC Error");
        return ESP_ERR_INVALID_CRC;
    }

    sht4x_convert_data(buffer, data);
    return ESP_OK;
}

esp_err_t sht4x_activate_heater(sht4x_heater_t heater_mode, sht4x_data_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = sht4x_send_cmd(heater_mode);
    if (ret != ESP_OK) return ret;

    // Wait for heating + measurement duration
    // Long pulse = 1s, Short pulse = 0.1s
    TickType_t wait_ticks;
    if (heater_mode == SHT4X_HEATER_200MW_1S || 
        heater_mode == SHT4X_HEATER_110MW_1S || 
        heater_mode == SHT4X_HEATER_20MW_1S) {
        wait_ticks = pdMS_TO_TICKS(1100); 
    } else {
        wait_ticks = pdMS_TO_TICKS(110);
    }
    vTaskDelay(wait_ticks);

    uint8_t buffer[6];
    ret = i2c_master_read_from_device(SHT40_I2C_PORT, SHT40_ADDRESS, buffer, 6, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;

    if (sht4x_crc8(&buffer[0], 2) != buffer[2] || sht4x_crc8(&buffer[3], 2) != buffer[5]) {
        ESP_LOGE(TAG, "Heater measurement CRC Error");
        return ESP_ERR_INVALID_CRC;
    }

    sht4x_convert_data(buffer, data);
    return ESP_OK;
}

esp_err_t sht4x_soft_reset(void) {
    return sht4x_send_cmd(CMD_SOFT_RESET);
}