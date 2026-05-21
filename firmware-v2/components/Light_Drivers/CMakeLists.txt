idf_component_register(
    SRCS "source/ws2812b.c"
         "source/sht4x.c"
         "source/env_driver.c"
         "source/led_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES driver BSP BLE_Drivers BMS_Drivers A121_Drivers nvs_flash
)
