idf_component_register(SRCS "src/ble_driver.cpp"
                    INCLUDE_DIRS "src"
                    REQUIRES esp32_BNO08x NimBLE)