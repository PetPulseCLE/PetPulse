idf_component_register(SRCS "src/ble_driver.cpp"
                    INCLUDE_DIRS "src"
                    REQUIRES imu_driver esp32_BNO08x NimBLE)