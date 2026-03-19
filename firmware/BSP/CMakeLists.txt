idf_component_register(
    SRCS "source/bsp.c"
         "source/rtc_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_driver_spi
    PRIV_REQUIRES esp_timer nvs_flash
)
