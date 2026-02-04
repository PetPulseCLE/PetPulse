idf_component_register(SRCS "imu_driver.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp32_BNO08x
                    )