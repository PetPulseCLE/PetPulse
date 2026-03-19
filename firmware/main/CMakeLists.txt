if(APP STREQUAL "unified")
    set(MAIN_SRC "app_main.c")
    message(STATUS "Unified mode → app_main.c")
else()
    set(MAIN_SRC "main.cpp")
    message(STATUS "Other mode → main.cpp")
endif()

idf_component_register(
    SRCS "${MAIN_SRC}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        BSP
        BMS_Drivers
        NFC_Drivers
        BLE_Drivers
        A121_Drivers
        findmy_Drivers
        imu_driver
        esp32_BNO08x
        ble_driver
        bt
        nvs_flash
        driver
        esp_driver_spi
        esp_driver_gpio
        NimBLE
)