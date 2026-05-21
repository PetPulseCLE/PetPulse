idf_component_register(
    SRCS "source/ble_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash BSP BMS_Drivers 
)
