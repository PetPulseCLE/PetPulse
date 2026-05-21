idf_component_register(
    SRCS "source/nfc_driver.c"
         "source/nfc_tech_iso14443a.c"
         "source/nfc_tech_iso14443b.c"
         "source/nfc_tech_felica.c"
         "source/nfc_tech_iso15693.c"
         "source/st25r3916_driver.c"
         "source/nfc_ndef.c"
         "source/nfc_pairing.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_spi BSP
    PRIV_REQUIRES esp_timer BLE_Drivers
)
