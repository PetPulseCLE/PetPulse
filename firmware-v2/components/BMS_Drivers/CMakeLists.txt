idf_component_register(
    SRCS "source/bms_driver.c"
         "source/bq25896.c"
         "source/max17260.c"
    INCLUDE_DIRS "include"
    REQUIRES driver BSP esp_driver_usb_serial_jtag
    PRIV_REQUIRES IMU_Drivers
)
