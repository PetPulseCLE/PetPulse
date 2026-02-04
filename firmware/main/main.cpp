#include <stdio.h>
#include "BNO08x.hpp"
#include "imu_driver.hpp"
#include "esp_rom_sys.h"

extern "C" void app_main(void) {

    if(!imu_init()) {
        esp_rom_printf("IMU initialization failed!\n");
        return;
    }

    esp_rom_printf("IMU initialization successful!\n");

    // Test FRS read/write functions
    esp_rom_printf("\n=== Testing FRS Config Functions ===\n");

    // Test Significant Motion config - READ current
    imu_sig_motion_config_t sig_config;
    esp_rom_printf("Reading current config...\n");
    if (imu_get_sig_motion_config(sig_config)) {
        imu_print_sig_motion_config(sig_config);
    } else {
        esp_rom_printf("Failed to read sig motion config\n");
    }

    // WRITE new config: 3 steps at 10 m/s²
    sig_config.accel_threshold_ms2 = 10.0f;
    sig_config.step_threshold = 3;
    esp_rom_printf("\nWriting new config: 3 steps @ 10 m/s^2...\n");
    if (imu_set_sig_motion_config(sig_config)) {
        esp_rom_printf("Write successful!\n");
    } else {
        esp_rom_printf("Write failed!\n");
    }

    // READ back to verify
    esp_rom_printf("\nVerifying write...\n");
    if (imu_get_sig_motion_config(sig_config)) {
        imu_print_sig_motion_config(sig_config);
    }

    // Dump raw FRS record
    esp_rom_printf("\n=== Raw FRS Dump ===\n");
    imu_frs_dump(BNO08xFrsID::SIG_MOTION_DETECT_CONFIG);


}