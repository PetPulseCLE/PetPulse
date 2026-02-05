# PetPulse Firmware

Firmware for the PetPulse, built with ESP-IDF for the ESP32-S3.

## Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)

## Getting Started

1. Download managed components:
   ```bash
   idf.py reconfigure
   # This fetches the `esp32_BNO08x` driver from the component registry.
   ```

## Project Structure

```
├── CMakeLists.txt          Project configuration
├── main/
│   ├── CMakeLists.txt      Main component config
│   └── main.cpp            Application entry point
├── components/
│   └── imu_driver/         Custom IMU driver wrapper
├── managed_components/     Downloaded dependencies (auto-generated)
│   └── esp32_BNO08x/       BNO08x sensor driver
└── sdkconfig               ESP-IDF configuration
```

## Dependencies


- [esp32_BNO08x](https://github.com/notwillhynds/esp32_BNO08x) - BNO08x IMU driver for ESP-IDF 
```
Important: Use lib/sig-motion-ext branch
```
