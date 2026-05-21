idf_component_register(
    SRCS "app_main.c"
    REQUIRES BSP BMS_Drivers Light_Drivers NFC_Drivers BLE_Drivers A121_Drivers FindMy_Drivers IMU_Drivers
             bt nvs_flash driver esp_driver_spi esp_driver_gpio
)
