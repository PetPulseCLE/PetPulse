idf_component_register(
    SRCS "source/findmy.c"
         "source/findmy_keys.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash BLE_Drivers BMS_Drivers
)
