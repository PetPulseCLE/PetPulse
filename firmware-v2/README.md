# PetPulse Firmware

Firmware for the PetPulse wearable, built with ESP-IDF for the ESP32-S3 (N16R8).

## Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- ESP32-S3 N16R8 target

## Getting Started

1. Set target and configure:
   ```bash
   idf.py set-target esp32s3
   idf.py reconfigure
   ```
2. Build, flash, and monitor:
   ```bash
   idf.py build flash monitor
   ```

## Project Structure

```
├── CMakeLists.txt              Project configuration
├── sdkconfig.defaults          Default Kconfig values
├── BSP/                        Board support: I2C/SPI bus init, GPIO/ISR, RTC
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c              Application entry point
├── components/
│   ├── A121_Drivers/           Acconeer A121 radar HAL
│   ├── BLE_Drivers/            NimBLE GATT server (vitals, motion, env, battery, CTS)
│   ├── BMS_Drivers/            BQ25896 charger + MAX17260 fuel gauge + power manager
│   ├── Light_Drivers/          SHT4x temperature/humidity
│   ├── NFC_Drivers/            NFC driver
│   ├── FindMy_Drivers/         Apple Find My (cloned AirTag) beacon
│   └── IMU_Drivers/            BNO08x IMU (motion, steps, activity classifier)
```

## Components Overview

|    Component     |                                   Purpose                                      |
|------------------|--------------------------------------------------------------------------------|
| `BSP`            | Owns shared I2C master bus, SPI buses, GPIO ISR service, RTC driver            |
| `BLE_Drivers`    | C BLE driver over NimBLE: custom PetPulse services + standard BAS/CTS/DIS      |
| `BMS_Drivers`    | Charger/fuel-gauge control, USB-host vs adapter source detection, power events |
| `IMU_Drivers`    | BNO08x motion reports, NVS-persisted daily step counter, motion/static state   |
| `FindMy_Drivers` | Find My advertising via captured MAC + payload                                 |
| `Light_Drivers`  | SHT4x environment readings                                                     |
| `A121_Drivers`   | Acconeer A121 radar HAL integration                                            |
| `NFC_Drivers`    | NFC tag interface                                                              |

## Target

- MCU: ESP32-S3 N16R8
- IDF: v5.5.2
- BLE stack: NimBLE (via `bt` component)

## Notes

- I2C uses the new `driver/i2c_master.h` API — the bus is owned by BSP and devices register per-driver handles.