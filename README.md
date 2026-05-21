# PetPulse

An AI-powered smart pet collar that tracks vital signs, activity, and behavior to keep your pet healthy and safe.

## Contents

- [Codebase Structure](#codebase-structure)
- [Backend AI/ML](#backend-aiml)
- [Firmware](#firmware)
- [Mobile App](#mobile-app)

---

## Codebase Structure

| Path               | Description                    |
| ------------------ | ------------------------------ |
| `backend/`         | [Backend AI/ML](#backend-aiml) |
| `database/`        | Supabase Cloud                 |
| `firmware-v2/`     | [Firmware](#firmware)          |
| `petpulse-mobile/` | [Mobile App](#mobile-app)      |
| `scripts/`         | Dev scripts                    |

---

## Backend AI/ML

_FastAPI services for daily Gemini summaries and 30-day health trends._

### Prerequisites

| Database & API      | Data Science & ML      | AI SDKs                | Utilities                   | Testing          |
| ------------------- | ---------------------- | ---------------------- | --------------------------- | ---------------- |
| `supabase >=2.0.0`  | `pandas ==2.2.3`       | `google-genai >=1.0.0` | `pydantic >=2.0.0`          | `pytest ==9.0.2` |
| `fastapi >=0.115.0` | `numpy ==2.4.3`        |                        | `pydantic-settings >=2.0.0` |                  |
| `uvicorn >=0.30.0`  | `scikit-learn ==1.6.1` |                        | `python-dotenv >=1.0.0`     |                  |

### Getting Started

1. AISummary & TrendsEngine:
   ```bash
   uv venv
   source .venv/bin/activate
   uv pip install -r requirements.txt
   uv run python -m uvicorn main:app --reload
   ```

### Project Structure

```
backend/AI-ML/
├── AISummary/                    Daily Gemini health summaries
│   ├── api/                      HTTP routes
│   ├── repository/               Supabase queries
│   ├── services/                 AI, clinical, and demo-time logic
│   ├── scripts/                  Dev and E2E scripts
│   ├── main.py                   FastAPI entry point
│   ├── config.py                 Env and demo-mode settings
│   ├── models.py                 Health snapshot schemas
│   ├── symptoms.json             Clinical reference data
│   └── requirements.txt          Dependencies
└── TrendsEngine/                 30-day trend analytics
    ├── db/                       Supabase client
    ├── models/                   API response schemas
    ├── routers/                  HTTP routes
    ├── services/                 Z-score and IsolationForest pipeline
    ├── sql/                      Daily materialized view definitions
    ├── main.py                   FastAPI entry point
    ├── test_trends.py            Offline pytest suite
    └── requirements.txt          Dependencies
```

### Components Overview

| Component      | Purpose                                                                                   |
| -------------- | ----------------------------------------------------------------------------------------- |
| `AISummary`    | RAG-lite daily summary: Supabase snapshot → anomaly thresholds → clinical lookup → Gemini |
| `TrendsEngine` | 30-day trend cards: materialized views → Z-score + IsolationForest → Expo-ready JSON      |

---

## Firmware

_ESP-IDF firmware for the ESP32-S3 wearable collar._

### Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- ESP32-S3 N16R8 target

### Getting Started

1. Set target and configure:
   ```bash
   idf.py set-target esp32s3
   idf.py reconfigure
   ```
2. Build, flash, and monitor:
   ```bash
   idf.py build flash monitor
   ```

### Project Structure

```
├── CMakeLists.txt              Project configuration
├── sdkconfig.defaults          Default Kconfig values
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

### Components Overview

| Component        | Purpose                                                                        |
| ---------------- | ------------------------------------------------------------------------------ |
| `BSP (Redacted)` | Owns shared I2C master bus, SPI buses, GPIO ISR service, RTC driver            |
| `BLE_Drivers`    | C BLE driver over NimBLE: custom PetPulse services + standard BAS/CTS/DIS      |
| `BMS_Drivers`    | Charger/fuel-gauge control, USB-host vs adapter source detection, power events |
| `IMU_Drivers`    | BNO08x motion reports, NVS-persisted daily step counter, motion/static state   |
| `FindMy_Drivers` | Find My advertising via captured MAC + payload                                 |
| `Light_Drivers`  | SHT4x environment readings                                                     |
| `A121_Drivers`   | Acconeer A121 radar HAL integration                                            |
| `NFC_Drivers`    | NFC tag interface                                                              |

### Target

- MCU: ESP32-S3 N16R8
- IDF: v5.5.2
- BLE stack: NimBLE (via `bt` component)

### Notes

- I2C uses the new `driver/i2c_master.h` API — the bus is owned by BSP and devices register per-driver handles.

---

## Mobile App

_Expo (React Native) app for BLE device pairing, live vitals, and Supabase sync._

### Prerequisites

- Node.js 18+
- [Expo dev client](https://docs.expo.dev/develop/development-builds/introduction/) (required for BLE and SecureStore; Expo Go is not supported)

### Getting Started

1. Install dependencies:
   ```bash
   cd petpulse-mobile
   npm install
   ```
2. Start with the iOS dev client:
   ```bash
   npm run ios:start
   ```

### Project Structure

```
petpulse-mobile/
├── app/                          Expo Router screens (auth, onboarding, tabs)
├── components/
│   ├── petpulse-ui/              App-specific UI (charts, BLE modal, metric cards)
│   └── ui/                       Shared UI primitives
├── hooks/ble/                    BLE scan, connect, time sync, sensor updates
├── lib/petpulse/                 Sensor parsing, Supabase data service, AI summaries
├── context/                      Auth, BLE, and theme providers
└── constants/                    Theme tokens and health ranges
```

### Components Overview

| Area            | Purpose                                                            |
| --------------- | ------------------------------------------------------------------ |
| `hooks/ble/`    | Device scan/connect, binary sensor parsing, live Supabase inserts  |
| `lib/petpulse/` | Data fetching, chart summaries, AI summary service integration     |
| `app/(tabs)/`   | Dashboard and per-metric detail screens (HR, activity, temp, etc.) |
| `context/`      | Auth state, BLE provider, theme preferences                        |
