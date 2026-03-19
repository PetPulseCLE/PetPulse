# IMU Data Pipeline Design
**Date:** 2026-03-17
**Branch:** firmware/merge
**Status:** Approved for implementation

---

## Overview

Design for the IMU data pipeline on the PetPulse firmware. The pipeline reads sensor data from the BNO08x IMU, filters by accuracy where applicable, timestamps each record, and routes to BLE notification or SD card fallback based on connection quality. Two FreeRTOS tasks are defined entirely within `imu_driver.cpp` and exposed via `imu_driver.hpp` for `xTaskCreatePinnedToCore` calls in `app_main.c`.

---

## Decisions Summary

| Decision | Choice |
|----------|--------|
| Accuracy filter | `>= BNO08xAccuracy::MED` for vector sensors only (accel, gyro, magf, RV); also drops `UNDEFINED`; NOT applied to step_counter or activity_classifier |
| Background sensors | step_counter, activity_classifier @ 10s |
| Live sensors | step_counter, activity_classifier @ 5s |
| Dev sensors | accel, gyro, magf, RV, step_counter, activity_classifier @ 1s |
| Timestamp source | UTC via `getUTCTimestamp()`; block on `BLE_AUTHENTICATED_BIT \| BLE_TIME_SYNCED_BIT` every power cycle |
| BLE gate | `bleServer.isAuthenticated()` AND `ble_get_rssi() >= -80 dBm` |
| SD fallback | Stub — single `sd_write_aggregated_record(const BleAggregatedAll_t &)` using presence bitmask |
| Sig motion action | `xEventGroupSetBits(imuEventGroup, IMU_EVT_MOTION_BIT)`; re-arm + re-register callback in `imu_motion_task` body |
| IMU event group | `imuEventGroup` + `IMU_EVT_MOTION_BIT` defined in `imu_driver.hpp/.cpp`; created by `imu_events_init()` |
| Task location | Both tasks in `imu_driver.cpp`, exposed via `imu_driver.hpp` |
| RSSI function | New `ble_get_rssi()` in `ble_driver`; returns `-127` when no connection |
| CPU sleep state | Non-sleep assumed; no light-sleep optimizations required |
| Supersedes | Existing `data_processing_task` period constants in `imu_driver.cpp` (Background=20s, Live=10s) — replace with Background=10s, Live=5s |

---

## Architecture

```cpp
// app_main.c — order matters: imu_events_init() before xTaskCreate
imu_events_init();   // creates imuEventGroup
xTaskCreatePinnedToCore(imu_data_task,   "imu_data",   8192, NULL, 4, NULL, core);
xTaskCreatePinnedToCore(imu_motion_task, "imu_motion", 4096, NULL, 3, NULL, core);
// Note: imu_init() is called inside imu_data_task before enabling reports.
// It is NOT called from app_main.
```

> **C/C++ boundary:** `imu_driver.hpp` wraps only the three new C-callable prototypes (`imu_data_task`, `imu_motion_task`, `imu_events_init`) in a dedicated `extern "C"` block, placed **after** all C++ `#include` directives (including `BNO08xGlobalTypes.hpp`). The rest of the header remains C++ as-is. Example:
> ```cpp
> // ... existing C++ includes and declarations ...
> #ifdef __cplusplus
> extern "C" {
> #endif
> void imu_events_init(void);
> void imu_data_task(void *pv);
> void imu_motion_task(void *pv);
> #ifdef __cplusplus
> }
> #endif
> ```

---

### Data Flow

```
BNO08x cb_task (priority 5)
  └─ fires per-report callbacks registered on imu.rpt.<report>

Per-vector-sensor callback (accel, gyro, magf, RV — Dev mode only):
  ├─ accuracy < MED or accuracy == UNDEFINED?  → return (drop)
  ├─ getUTCTimestamp()
  ├─ bleServer.isAuthenticated() && ble_get_rssi() >= -80?
  │    yes → bleServer.updateX() + setRaw()
  │    no  → sd_write_raw_record() [stub]
  └─ return

Per-classifier callback (step_counter, activity_classifier — all modes):
  ├─ getUTCTimestamp()   ← no accuracy filter
  ├─ bleServer.isAuthenticated() && ble_get_rssi() >= -80?
  │    yes → bleServer.updateX() + setActivity()
  │    no  → sd_write_activity_record() [stub]
  └─ return

imu_data_task startup (before main loop):
  1. imu_init()
  2. xEventGroupWaitBits(bleEventGroup, BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT, ...)
  3. imu_enable_rpt() for sensors in current mode
  4. imu_register_callbacks(current mode)

imu_data_task main loop:
  └─ monitor bleServer.getMode() for changes
       Background ↔ Live: no hardware change — callbacks self-gate via esp_timer_get_time()
       Any → Dev:    enable + register_cb for accel, gyro, magf, rv
       Dev → Any:    disable + unregister accel, gyro, magf, rv
       vTaskDelay(1000ms)

imu_motion_task:
  1. imu_rearm_sig_motion()
  2. imu.rpt.significant_motion.register_cb(sig_motion_cb)
  3. while(true):
       xEventGroupWaitBits(imuEventGroup, IMU_EVT_MOTION_BIT, pdTRUE, pdFALSE, portMAX_DELAY)
       imu_rearm_sig_motion()
       imu.rpt.significant_motion.register_cb(sig_motion_cb)   ← re-register after re-arm

Sig motion callback (fires from BNO08x cb_task):
  └─ xEventGroupSetBits(imuEventGroup, IMU_EVT_MOTION_BIT)
     ← no re-arm here; imu_motion_task handles it on wake
```

---

## Callback Registration

Both tasks access `imu.rpt.<report>` **directly** — they are inside `imu_driver.cpp` and therefore have access to the file-scope static `BNO08x imu` instance. No new public API wrappers are needed.

Per-report registration:
```cpp
imu.rpt.accelerometer.register_cb(imu_accel_cb);
imu.rpt.cal_gyro.register_cb(imu_gyro_cb);
imu.rpt.cal_magnetometer.register_cb(imu_magf_cb);
imu.rpt.rv.register_cb(imu_rv_cb);
imu.rpt.step_counter.register_cb(imu_step_cb);
imu.rpt.activity_classifier.register_cb(imu_activity_cb);
imu.rpt.significant_motion.register_cb(sig_motion_cb);
```

A static helper `imu_register_callbacks(Mode m)` inside `imu_driver.cpp` enables sensors at `IMU_HW_PERIOD_US` and registers callbacks. Called once at `imu_data_task` startup with the initial mode.

Subsequent mode changes do **not** call `imu_disable_all_rpts()`:
- Background ↔ Live: no hardware action — callbacks self-gate
- Entering Dev: enable + register_cb for raw motion sensors only
- Leaving Dev: `imu.rpt.<report>.disable()` for each raw motion sensor (this also clears their callbacks)

> **Note:** `imu.rpt.<report>.disable()` clears registered callbacks for that report. Re-registration is only needed when transitioning into Dev mode (enable + register_cb paired). Callbacks for step_counter and activity_classifier are registered once at init and never cleared.

> **Sig motion note:** `imu_rearm_sig_motion()` calls disable+enable internally, clearing the sig motion callback. `imu_motion_task` always re-registers `sig_motion_cb` immediately after each re-arm call.

---

## Mode → Sensor Mapping

The BNO08x minimum hardware report period is ~10ms (100Hz). Sensors are always enabled at the hardware rate (`100000UL` µs / 10Hz). The mode controls a **software clock gate** in each callback — data is discarded unless enough time has elapsed since the last forwarded reading.

```cpp
// Hardware enable rate — fixed, never changes with mode
#define IMU_HW_PERIOD_US  100000UL  // 10Hz

// Software gate threshold per mode
static uint64_t mode_to_period_us(Mode m) {
    switch (m) {
        case Background: return 10000000ULL;  // 10s
        case Live:       return  5000000ULL;  //  5s
        case Dev:        return  1000000ULL;  //  1s
        default:         return 10000000ULL;
    }
}
```

Per-callback gate (example for accel):
```cpp
static uint64_t last_accel_us = 0;

static void imu_accel_cb() {
    uint64_t now = esp_timer_get_time();
    if ((now - last_accel_us) < mode_to_period_us(bleServer.getMode())) return;
    last_accel_us = now;
    // accuracy filter → timestamp → route
}
```

Each sensor has its own `last_sent_us` static variable. Mode changes in the task loop update the gate threshold immediately — no disable/re-enable of hardware reports needed.

| Mode | Gate Period | Enabled Sensors (HW always at 10Hz) |
|------|-------------|--------------------------------------|
| Background | 10s | step_counter, activity_classifier |
| Live | 5s | step_counter, activity_classifier |
| Dev | 1s | accel, gyro, magf, rv, step_counter, activity_classifier |

**Mode transitions:**
- Background ↔ Live: no hardware change — only gate threshold changes
- Any mode → Dev: additionally enable raw motion sensors at `IMU_HW_PERIOD_US`
- Dev → any mode: disable raw motion sensors (`imu.rpt.accelerometer.disable()` etc.) + unregister their callbacks

---

## Timestamp Behavior

- `getUTCTimestamp()` (defined in `ble_driver.hpp`, uses `<sys/time.h>`) provides UTC time.
- ESP32 internal RTC does **not** survive full power cycles.
- `imu_data_task` blocks at startup until both BLE conditions are met:
  ```cpp
  xEventGroupWaitBits(bleEventGroup,
      BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT,
      pdFALSE, pdTRUE,   // don't clear, wait for ALL bits
      portMAX_DELAY);
  ```
- Data collection does not begin until the phone connects, authenticates, and syncs time via the Current Time Service (CTS).

---

## Accuracy Filtering

BNO08x accuracy is a 5-value enum:

```cpp
enum class BNO08xAccuracy : uint8_t {
    UNRELIABLE = 0,  // dropped
    LOW        = 1,  // dropped
    MED        = 2,  // kept
    HIGH       = 3,  // kept
    UNDEFINED  = 4,  // dropped — treat same as UNRELIABLE
};
```

Applied only to vector sensors (accel, gyro, magf, RV — Dev mode only):
```cpp
if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;
```

**Not applied to step_counter or activity_classifier** — the BNO08x SH-2 spec defines the accuracy field on these sensors differently from calibration quality. These sensors typically report LOW/UNRELIABLE regardless of data validity. Filtering them would silently drop all valid readings in Background and Live modes.

---

## BLE Transmission Gate

Two conditions must both be true to transmit over BLE:

1. `bleServer.isAuthenticated()` — returns `hasSubscriber() && _authenticated` (internal `std::atomic<bool>` set in `onAuthenticationComplete`, plus connected subscriber check). Does **not** directly read `bleEventGroup` bits.
2. `ble_get_rssi() >= -80` — new function in `ble_driver` wrapping NimBLE's RSSI query.

**`ble_get_rssi()` contract:**
- Returns current RSSI in dBm as `int8_t` when connected.
- Returns `-127` when no connection — ensures gate fails without relying solely on `isAuthenticated()`.

---

## IMU Event Group

Created by `imu_events_init()` — called from `app_main` **before** either task is created.

Defined in `imu_driver.hpp`:
```cpp
#define IMU_EVT_MOTION_BIT  (1 << 0)
// Future: IMU_EVT_FALL_BIT (1<<1), IMU_EVT_SHAKE_BIT (1<<2)

extern EventGroupHandle_t imuEventGroup;
```

Any task can watch `imuEventGroup` by including `imu_driver.hpp`.

---

## Significant Motion Task

**Priority: 3** — below BNO08x `cb_task` (priority 5) and `imu_data_task` (priority 4).

```cpp
void imu_motion_task(void *pv) {
    imu_rearm_sig_motion();
    imu.rpt.significant_motion.register_cb(sig_motion_cb);
    while (true) {
        xEventGroupWaitBits(imuEventGroup, IMU_EVT_MOTION_BIT,
                            pdTRUE, pdFALSE, portMAX_DELAY);
        // re-arm + re-register (disable clears callback)
        imu_rearm_sig_motion();
        imu.rpt.significant_motion.register_cb(sig_motion_cb);
    }
}

static void sig_motion_cb(void) {
    // Called from BNO08x cb_task — a FreeRTOS task, not an ISR.
    // Use xEventGroupSetBits (non-ISR variant).
    xEventGroupSetBits(imuEventGroup, IMU_EVT_MOTION_BIT);
}
```

---

## SD Card Stub

Uses `BleAggregatedAll_t`, which is moved to **file scope** in `ble_driver.hpp` (same pattern as `Timestamp_t` — defined above the `BleServer` class, not nested inside it). This makes it directly usable in `imu_driver.cpp` and any other component.

The `presence_bitmask` field indicates which sub-structs are populated for a given write:

```cpp
// Presence bitmask bits (defined in ble_driver.hpp alongside BleAggregatedAll_t)
#define AGG_PRESENCE_RAW_BIT        (1 << 0)  // accel, gyro, magf, rv populated
#define AGG_PRESENCE_ACTIVITY_BIT   (1 << 1)  // step_counter, activity_classifier populated
#define AGG_PRESENCE_VITALS_BIT     (1 << 2)  // breath_rate, heart_rate populated
#define AGG_PRESENCE_ENV_BIT        (1 << 3)  // temperature, humidity populated
```

Per-mode presence at IMU write time:
- Background / Live: `AGG_PRESENCE_ACTIVITY_BIT` only
- Dev: `AGG_PRESENCE_RAW_BIT | AGG_PRESENCE_ACTIVITY_BIT`

```cpp
// File-scope in imu_driver.cpp
// TODO: replace with real sd_driver implementation
static esp_err_t sd_write_aggregated_record(const BleAggregatedAll_t &record) {
    return ESP_OK;
}
```

**`ble_driver.hpp` change:** Move `BleAggregatedAll_t` and its component structs (`BleRaw_t`, `BleActivity_t`, `BleVitals_t`, `BleEnv_t`, `BleAccel_t`, `BleGyro_t`, `BleMagf_t`, `BleRV_t`, `BleStepCount_t`, `BleActivityClass_t`) from `private:` inside `BleServer` to file scope above the class definition — mirroring how `Timestamp_t` is declared. The `BleServer` private members become references or instances of the now-public types.

---

## New Additions to Existing Files

| File | Change |
|------|--------|
| `imu_driver.hpp` | Add `extern "C"` block with `imu_events_init`, `imu_data_task`, `imu_motion_task`; add `imuEventGroup`, `IMU_EVT_MOTION_BIT` |
| `imu_driver.cpp` | Implement both tasks, `imu_register_callbacks(Mode)` helper, `imu_events_init()`, static callbacks, `sd_write_aggregated_record` stub; update period constants |
| `ble_driver.hpp` | Move `BleAggregatedAll_t` and all component structs to file scope above `BleServer`; add `AGG_PRESENCE_*` bitmask defines |
| `ble_driver.hpp/.cpp` | Add `int8_t ble_get_rssi()` — returns dBm or `-127` when disconnected |
| `app_main.c` | Call `imu_events_init()`, then `xTaskCreatePinnedToCore` for both IMU tasks |

---

## What Is NOT in Scope

- SD card driver implementation (stubbed)
- RSSI-based adaptive sampling
- Shake detector, tap detector, stability classifier (not enabled in any mode)

## Future Work

- **Light sleep / wake via sig motion:** `imu_motion_task` will eventually manage ESP32 light sleep entry and use the BNO08x INT GPIO pin as the wake source (`esp_sleep_enable_gpio_wakeup(imu_get_int_pin(), ...)`). On wake, `esp_sleep_get_wakeup_cause()` is checked and `IMU_EVT_MOTION_BIT` is set before re-entering sleep. The sig motion callback infrastructure designed here is the foundation for this — no rework needed when this is added.
