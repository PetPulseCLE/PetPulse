idf_component_register(
    SRC_DIRS "src"
             "bno08x_source"
             "bno08x_source/report"
             "SH2"
    INCLUDE_DIRS "src"
                 "bno08x_include"
                 "bno08x_include/report"
                 "bno08x_include/callback"
                 "SH2"
                 "."
    REQUIRES driver esp_timer BSP
    PRIV_REQUIRES cmock BLE_Drivers nvs_flash
)
