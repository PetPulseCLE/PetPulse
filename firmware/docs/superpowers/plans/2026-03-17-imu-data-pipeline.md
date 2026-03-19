# IMU Data Pipeline Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a two-task IMU data pipeline on ESP32/FreeRTOS that collects BNO08x sensor data, filters by accuracy, timestamps via UTC, and routes to BLE notify or SD stub based on connection quality and signal strength.

**Architecture:** Two FreeRTOS tasks (`imu_data_task`, `imu_motion_task`) live entirely in `imu_driver.cpp` and are wired into `app_main.c` via `xTaskCreatePinnedToCore`. Sensors run at hardware rate (10Hz); a software clock gate per callback controls how often data is forwarded based on the current `Mode` (Background=10s, Live=5s, Dev=1s). Significant motion detection wakes the system via a FreeRTOS event group bit.

**Tech Stack:** ESP-IDF, FreeRTOS, C++17, BNO08x (esp32_BNO08x managed component), NimBLE (via arduino-esp32 NimBLE port), `esp_timer`, `idf.py build` for verification.

**Spec:** `docs/superpowers/specs/2026-03-17-imu-data-pipeline-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `components/ble_driver/src/ble_driver.hpp` | Modify | Move Ble* structs to file scope; add AGG_PRESENCE_* defines |
| `components/ble_driver/src/ble_driver.cpp` | Modify | Add `ble_get_rssi()` implementation |
| `components/imu_driver/src/imu_driver.hpp` | Modify | Add `extern "C"` block, `imuEventGroup`, `IMU_EVT_MOTION_BIT` |
| `components/imu_driver/src/imu_driver.cpp` | Modify | Add both tasks, all callbacks, helpers, SD stub, event init |
| `main/app_main.c` | Modify | Call `imu_events_init()` + `xTaskCreatePinnedToCore` for both tasks |

---

## Task 1: Move BleServer structs to file scope in `ble_driver.hpp`

`BleAggregatedAll_t` and all its component structs are currently private nested types inside `BleServer`. They need to be file-scope so `imu_driver.cpp` can use `BleAggregatedAll_t` for the SD stub and the aggregated BLE write.

**Files:**
- Modify: `components/ble_driver/src/ble_driver.hpp`

- [ ] **Step 1: Read the file to understand current struct layout**

  Open `components/ble_driver/src/ble_driver.hpp`. Note that `BleAccel_t`, `BleGyro_t`, `BleMagf_t`, `BleRV_t`, `BleStepCount_t`, `BleActivityClass_t`, `BleVitals_t`, `BleEnv_t`, `BleRaw_t`, `BleActivity_t`, and `BleAggregatedAll_t` are all declared inside `private:` in `BleServer` (lines ~113–310). `Timestamp_t` (lines 55–63) is the model — it lives above the class.

- [ ] **Step 2: Cut all Ble* struct definitions out of `private:` and paste them above the `BleServer` class definition**

  Place them immediately after the `#define` blocks and before `struct Timestamp_t`. Keep the `__attribute__((packed))` on each. The `operator=` assignments that reference `bno08x_*` types remain unchanged since `BNO08xGlobalTypes.hpp` is already included.

  Each struct in `private:` is currently defined AND immediately followed by a member variable declaration of that type. Both must be removed together. For example, the current pattern is:
  ```cpp
  struct BleAccel_t { ... } __attribute__((packed));
  BleAccel_t _accel;       // ← remove this line too
  ```
  Remove **both** the struct definition and its trailing member variable declaration for every Ble* struct being moved. Then replace the entire block with just the member variable declarations:
  ```cpp
  // private: section becomes — member vars only (structs now file-scope above):
  BleAccel_t         _accel;
  BleGyro_t          _gyro;
  BleMagf_t          _magf;
  BleRV_t            _rv;
  BleStepCount_t     _stepCount;
  BleActivityClass_t _activityClass;
  BleVitals_t        _vitals;
  BleEnv_t           _env;
  BleRaw_t           _raw;
  BleActivity_t      _activity;
  BleAggregatedAll_t _aggregatedAll;
  // Battery structs stay private (not needed externally) — leave in place
  ```

- [ ] **Step 3: Add `AGG_PRESENCE_*` bitmask defines alongside `BleAggregatedAll_t`**

  Immediately after the `BleAggregatedAll_t` struct definition (now file-scope), add:
  ```cpp
  #define AGG_PRESENCE_RAW_BIT        (1 << 0)
  #define AGG_PRESENCE_ACTIVITY_BIT   (1 << 1)
  #define AGG_PRESENCE_VITALS_BIT     (1 << 2)
  #define AGG_PRESENCE_ENV_BIT        (1 << 3)
  ```

- [ ] **Step 4: Verify build compiles cleanly**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:|warning:" | head -30
  ```
  Expected: zero errors. Warnings about unused variables in `BleServer` are acceptable.

- [ ] **Step 5: Commit**

  ```bash
  git add components/ble_driver/src/ble_driver.hpp
  git commit -m "refactor(ble): move BleAggregatedAll_t and component structs to file scope"
  ```

---

## Task 2: Add `ble_get_rssi()` to the BLE driver

New function returning current RSSI in dBm, or `-127` when disconnected. Used by IMU callbacks to gate BLE transmission.

**Files:**
- Modify: `components/ble_driver/src/ble_driver.hpp`
- Modify: `components/ble_driver/src/ble_driver.cpp`

- [ ] **Step 1: Add declaration to `ble_driver.hpp`**

  After the `syncSysTime` declaration (line ~73), add:
  ```cpp
  /**
   * @brief Get current BLE connection RSSI
   * @return RSSI in dBm if connected, -127 if no active connection
   */
  int8_t ble_get_rssi();
  ```

- [ ] **Step 2: Add implementation to `ble_driver.cpp`**

  After the `getUTCTimestamp()` function body (~line 58), add:
  ```cpp
  int8_t ble_get_rssi() {
      if (!bleServer.hasSubscriber()) return -127;
      // NimBLEConnInfo has no getRSSI() — use raw NimBLE stack call.
      // ble_gap_conn_rssi() is declared in host/ble_gap.h (included transitively).
      NimBLEConnInfo connInfo = NimBLEDevice::getServer()->getPeerInfo(0);
      int8_t rssi = 0;
      int rc = ble_gap_conn_rssi(connInfo.getConnHandle(), &rssi);
      if (rc != 0) return -127;
      return rssi;
  }
  ```

- [ ] **Step 3: Build and verify**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```
  Expected: zero errors.

- [ ] **Step 4: Commit**

  ```bash
  git add components/ble_driver/src/ble_driver.hpp components/ble_driver/src/ble_driver.cpp
  git commit -m "feat(ble): add ble_get_rssi() returning dBm or -127 when disconnected"
  ```

---

## Task 3: Add IMU event group declarations to `imu_driver.hpp`

Add the `imuEventGroup` handle, `IMU_EVT_MOTION_BIT` define, and `extern "C"` block for the three new C-callable functions. This is the public interface for cross-task motion signalling.

**Files:**
- Modify: `components/imu_driver/src/imu_driver.hpp`

- [ ] **Step 1: Add FreeRTOS event group include and declarations**

  At the top of `imu_driver.hpp`, after the existing `#include` lines and before `typedef struct imu_report_cfg_t`, add:
  ```cpp
  #include "freertos/FreeRTOS.h"
  #include "freertos/event_groups.h"

  /* IMU system event group — watch from any task via imuEventGroup */
  #define IMU_EVT_MOTION_BIT   (1 << 0)
  // Future: IMU_EVT_FALL_BIT (1<<1), IMU_EVT_SHAKE_BIT (1<<2)

  extern EventGroupHandle_t imuEventGroup;
  ```

- [ ] **Step 2: Add `extern "C"` block at the bottom of the header (before `#endif`)**

  Replace the `//TESTING FUNCTIONS` block at the bottom with:
  ```cpp
  // Legacy test tasks — keep for reference
  void motion_detection_task(void *pvParameters);
  void data_processing_task(void *pvParameters);

  #ifdef __cplusplus
  extern "C" {
  #endif

  /**
   * @brief Create the IMU event group. MUST be called from app_main
   *        before xTaskCreatePinnedToCore for either IMU task.
   */
  void imu_events_init(void);

  /**
   * @brief IMU data pipeline task — init, wait for BLE time sync,
   *        enable sensors, run clock-gated callbacks.
   *        Priority 4, stack 8192.
   */
  void imu_data_task(void *pv);

  /**
   * @brief Significant motion sentinel task — arms detector, sets
   *        IMU_EVT_MOTION_BIT on trigger, re-arms on wake.
   *        Priority 3, stack 4096.
   */
  void imu_motion_task(void *pv);

  #ifdef __cplusplus
  }
  #endif
  ```

- [ ] **Step 3: Build**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```
  Expected: zero errors.

- [ ] **Step 4: Commit**

  ```bash
  git add components/imu_driver/src/imu_driver.hpp
  git commit -m "feat(imu): add imuEventGroup, IMU_EVT_MOTION_BIT, extern C task prototypes"
  ```

---

## Task 4: Add static helpers and SD stub to `imu_driver.cpp`

Add the foundational pieces that all callbacks and tasks depend on: the event group definition, the period helper, per-sensor `last_sent_us` trackers, the `imu_register_callbacks()` helper, and the SD stub.

**Files:**
- Modify: `components/imu_driver/src/imu_driver.cpp`

- [ ] **Step 1: Add required includes near the top of `imu_driver.cpp`**

  After the existing includes, add:
  ```cpp
  #include "ble_driver.hpp"
  #include "esp_timer.h"
  #include "freertos/event_groups.h"
  ```

- [ ] **Step 2: Define `imuEventGroup` and `imu_events_init()` near the top of the file (after global `BNO08x imu` instance)**

  ```cpp
  EventGroupHandle_t imuEventGroup = nullptr;

  extern "C" void imu_events_init(void) {
      imuEventGroup = xEventGroupCreate();
  }
  ```

- [ ] **Step 3: Add `mode_to_period_us()` and `last_sent_us` statics**

  ```cpp
  #define IMU_HW_PERIOD_US  100000UL   // 10Hz hardware rate

  static uint64_t mode_to_period_us(Mode m) {
      switch (m) {
          case Background: return 10000000ULL;
          case Live:       return  5000000ULL;
          case Dev:        return  1000000ULL;
          default:         return 10000000ULL;
      }
  }

  // Per-sensor software clock gate timestamps
  static uint64_t last_accel_us    = 0;
  static uint64_t last_gyro_us     = 0;
  static uint64_t last_magf_us     = 0;
  static uint64_t last_rv_us       = 0;
  static uint64_t last_step_us     = 0;
  static uint64_t last_activity_us = 0;
  ```

- [ ] **Step 4: Add the SD stub**

  ```cpp
  // TODO: replace with real sd_driver implementation
  static esp_err_t sd_write_aggregated_record(const BleAggregatedAll_t &record) {
      (void)record;
      return ESP_OK;
  }
  ```

- [ ] **Step 5: Add `imu_register_callbacks()` forward-declaration stub (body comes in Task 6)**

  ```cpp
  static void imu_accel_cb();
  static void imu_gyro_cb();
  static void imu_magf_cb();
  static void imu_rv_cb();
  static void imu_step_cb();
  static void imu_activity_cb();
  static void sig_motion_cb();

  static void imu_register_callbacks(Mode m) {
      uint32_t hw = IMU_HW_PERIOD_US;

      // Always-on sensors (all modes)
      imu.rpt.step_counter.enable(hw);
      imu.rpt.step_counter.register_cb(imu_step_cb);

      imu.rpt.activity_classifier.enable(hw);
      imu.rpt.activity_classifier.register_cb(imu_activity_cb);

      // Dev mode only — raw motion
      if (m == Dev) {
          imu.rpt.accelerometer.enable(hw);
          imu.rpt.accelerometer.register_cb(imu_accel_cb);

          imu.rpt.cal_gyro.enable(hw);
          imu.rpt.cal_gyro.register_cb(imu_gyro_cb);

          imu.rpt.cal_magnetometer.enable(hw);
          imu.rpt.cal_magnetometer.register_cb(imu_magf_cb);

          imu.rpt.rv.enable(hw);
          imu.rpt.rv.register_cb(imu_rv_cb);
      }
  }
  ```

- [ ] **Step 6: Remove superseded period constants**

  Search `imu_driver.cpp` for any existing `IMU_PERIOD_BACKGROUND_US`, `IMU_PERIOD_LIVE_US`, `IMU_PERIOD_DEV_US` defines and any `period_for_mode()` helper. Delete them — they are replaced by `IMU_HW_PERIOD_US` + `mode_to_period_us()`.

- [ ] **Step 7: Build to confirm no errors before implementing callbacks**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```

- [ ] **Step 8: Commit**

  ```bash
  git add components/imu_driver/src/imu_driver.cpp
  git commit -m "feat(imu): add event group init, mode gating helpers, SD stub, callback scaffolding"
  ```

---

## Task 5: Implement per-sensor callbacks

Each callback: clock-gate check → (vector sensors only) accuracy filter → timestamp → BLE or SD.

**Files:**
- Modify: `components/imu_driver/src/imu_driver.cpp`

The helper macro used in every callback:
```cpp
#define IMU_BLE_FEASIBLE() \
    (bleServer.isAuthenticated() && ble_get_rssi() >= -80)
```

- [ ] **Step 1: Implement `imu_accel_cb`**

  ```cpp
  static void imu_accel_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_accel_us) < mode_to_period_us(bleServer.getMode())) return;
      last_accel_us = now;

      bno08x_accel_t data = imu.rpt.accelerometer.get();
      if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

      Timestamp_t ts = getUTCTimestamp();
      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateAccel(data);
          bleServer.setRaw();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_RAW_BIT;
          rec.raw.accel = data;
          rec.raw.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 2: Implement `imu_gyro_cb`**

  ```cpp
  static void imu_gyro_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_gyro_us) < mode_to_period_us(bleServer.getMode())) return;
      last_gyro_us = now;

      bno08x_gyro_t data = imu.rpt.cal_gyro.get();
      if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

      Timestamp_t ts = getUTCTimestamp();
      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateGyro(data);
          bleServer.setRaw();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_RAW_BIT;
          rec.raw.gyro = data;
          rec.raw.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 3: Implement `imu_magf_cb`**

  ```cpp
  static void imu_magf_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_magf_us) < mode_to_period_us(bleServer.getMode())) return;
      last_magf_us = now;

      bno08x_magf_t data = imu.rpt.cal_magnetometer.get();
      if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

      Timestamp_t ts = getUTCTimestamp();
      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateMagf(data);
          bleServer.setRaw();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_RAW_BIT;
          rec.raw.magf = data;
          rec.raw.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 4: Implement `imu_rv_cb`**

  ```cpp
  static void imu_rv_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_rv_us) < mode_to_period_us(bleServer.getMode())) return;
      last_rv_us = now;

      bno08x_quat_t data = imu.rpt.rv.get_quat();
      if (data.accuracy < BNO08xAccuracy::MED || data.accuracy == BNO08xAccuracy::UNDEFINED) return;

      Timestamp_t ts = getUTCTimestamp();
      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateRV(data);
          bleServer.setRaw();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_RAW_BIT;
          rec.raw.rv = data;
          rec.raw.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 5: Implement `imu_step_cb` (no accuracy filter)**

  ```cpp
  static void imu_step_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_step_us) < mode_to_period_us(bleServer.getMode())) return;
      last_step_us = now;

      bno08x_step_counter_t data = imu.rpt.step_counter.get();
      Timestamp_t ts = getUTCTimestamp();

      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateStepCount(data);
          bleServer.setActivity();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_ACTIVITY_BIT;
          rec.activity.stepCount = data;
          rec.activity.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 6: Implement `imu_activity_cb` (no accuracy filter)**

  ```cpp
  static void imu_activity_cb() {
      uint64_t now = esp_timer_get_time();
      if ((now - last_activity_us) < mode_to_period_us(bleServer.getMode())) return;
      last_activity_us = now;

      bno08x_activity_classifier_t data = imu.rpt.activity_classifier.get();
      Timestamp_t ts = getUTCTimestamp();

      if (IMU_BLE_FEASIBLE()) {
          bleServer.updateActivityClass(data);
          bleServer.setActivity();
      } else {
          BleAggregatedAll_t rec = {};
          rec.presence_bitmask = AGG_PRESENCE_ACTIVITY_BIT;
          rec.activity.activityClass = data;
          rec.activity.timestamp = ts;
          sd_write_aggregated_record(rec);
      }
  }
  ```

- [ ] **Step 7: Implement `sig_motion_cb`**

  ```cpp
  static void sig_motion_cb() {
      // BNO08x cb_task context — FreeRTOS task, not ISR. Use non-ISR variant.
      xEventGroupSetBits(imuEventGroup, IMU_EVT_MOTION_BIT);
  }
  ```

- [ ] **Step 8: Build**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```
  Expected: zero errors. Fix any `get()` / `get_quat()` mismatches by checking `BNO08xRptRV.hpp` — the RV report exposes `get_quat()`, others expose `get()`.

- [ ] **Step 9: Commit**

  ```bash
  git add components/imu_driver/src/imu_driver.cpp
  git commit -m "feat(imu): implement per-sensor callbacks with clock gating, accuracy filter, BLE/SD routing"
  ```

---

## Task 6: Implement `imu_data_task`

The main data collection task. Initialises the IMU, blocks until BLE is ready, enables sensors, then monitors mode changes.

**Files:**
- Modify: `components/imu_driver/src/imu_driver.cpp`

- [ ] **Step 1: Add the task**

  ```cpp
  extern "C" void imu_data_task(void *pv) {
      static const char *TAG = "IMU_Data";

      // 1. Init IMU hardware
      if (!imu_init()) {
          ESP_LOGE(TAG, "imu_init() failed");
          vTaskDelete(NULL);
          return;
      }

      // 2. Block until BLE authenticated AND time synced (every power cycle)
      ESP_LOGI(TAG, "Waiting for BLE auth + time sync...");
      xEventGroupWaitBits(bleEventGroup,
          BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT,
          pdFALSE,   // don't clear bits
          pdTRUE,    // wait for ALL bits
          portMAX_DELAY);
      ESP_LOGI(TAG, "BLE ready — starting IMU data collection");

      // 3. Enable sensors and register callbacks for current mode
      Mode current_mode = bleServer.getMode();
      imu_register_callbacks(current_mode);

      // 4. Monitor mode changes
      while (true) {
          Mode new_mode = bleServer.getMode();
          if (new_mode != current_mode) {
              ESP_LOGI(TAG, "Mode change: %d -> %d", (int)current_mode, (int)new_mode);

              if (current_mode == Dev) {
                  // Leaving Dev — disable raw motion sensors (also clears their callbacks)
                  imu.rpt.accelerometer.disable();
                  imu.rpt.cal_gyro.disable();
                  imu.rpt.cal_magnetometer.disable();
                  imu.rpt.rv.disable();
              }

              if (new_mode == Dev) {
                  // Entering Dev — enable + register raw motion sensors
                  uint32_t hw = IMU_HW_PERIOD_US;
                  imu.rpt.accelerometer.enable(hw);
                  imu.rpt.accelerometer.register_cb(imu_accel_cb);
                  imu.rpt.cal_gyro.enable(hw);
                  imu.rpt.cal_gyro.register_cb(imu_gyro_cb);
                  imu.rpt.cal_magnetometer.enable(hw);
                  imu.rpt.cal_magnetometer.register_cb(imu_magf_cb);
                  imu.rpt.rv.enable(hw);
                  imu.rpt.rv.register_cb(imu_rv_cb);
              }

              // Background <-> Live: no hardware change, gate threshold updates automatically
              current_mode = new_mode;
          }

          vTaskDelay(pdMS_TO_TICKS(1000));
      }
  }
  ```

- [ ] **Step 2: Build**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add components/imu_driver/src/imu_driver.cpp
  git commit -m "feat(imu): implement imu_data_task with mode-gated sensor pipeline"
  ```

---

## Task 7: Implement `imu_motion_task`

Sentinel task that arms the significant motion detector and sets `IMU_EVT_MOTION_BIT` when triggered, then re-arms.

**Files:**
- Modify: `components/imu_driver/src/imu_driver.cpp`

- [ ] **Step 1: Add the task**

  ```cpp
  extern "C" void imu_motion_task(void *pv) {
      static const char *TAG = "IMU_Motion";

      // Arm one-shot sig motion detector and register callback
      imu_rearm_sig_motion();
      imu.rpt.significant_motion.register_cb(sig_motion_cb);
      ESP_LOGI(TAG, "Significant motion detector armed");

      while (true) {
          // Block until motion callback sets the bit
          xEventGroupWaitBits(imuEventGroup,
              IMU_EVT_MOTION_BIT,
              pdTRUE,          // clear bit on exit
              pdFALSE,
              portMAX_DELAY);

          ESP_LOGI(TAG, "Significant motion detected — re-arming");

          // Re-arm and re-register (imu_rearm_sig_motion internally calls disable+enable,
          // which clears the callback — must re-register immediately after)
          imu_rearm_sig_motion();
          imu.rpt.significant_motion.register_cb(sig_motion_cb);
      }
  }
  ```

- [ ] **Step 2: Build**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add components/imu_driver/src/imu_driver.cpp
  git commit -m "feat(imu): implement imu_motion_task with sig motion detection and re-arm"
  ```

---

## Task 8: Wire up tasks in `app_main`

Add `imu_events_init()` and both `xTaskCreatePinnedToCore` calls. `imu_driver.hpp` includes C++ headers (`BNO08xPrivateTypes.hpp` with default member initializers) so `app_main.c` **cannot** include it as a `.c` file — the C compiler will reject it unconditionally. The rename to `.cpp` is **mandatory** and must happen before adding the include.

**Files:**
- Rename: `main/app_main.c` → `main/app_main.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `main/app_main.cpp`

- [ ] **Step 1: Rename `app_main.c` to `app_main.cpp`**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware/main
  git mv app_main.c app_main.cpp
  ```

- [ ] **Step 2: Update `main/CMakeLists.txt` to reference `app_main.cpp`**

  The file currently has `set(MAIN_SRC "app_main.c")` inside the `if(APP STREQUAL "unified")` branch. Change it:
  ```cmake
  if(APP STREQUAL "unified")
      set(MAIN_SRC "app_main.cpp")
      message(STATUS "Unified mode → app_main.cpp")
  ```

- [ ] **Step 3: Verify build still compiles after rename only (no new includes yet)**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:" | head -20
  ```
  Expected: zero errors. The file content is unchanged; only the compiler changed from C to C++.

- [ ] **Step 4: Add the include**

  In `app_main.cpp`, after the existing includes, add:
  ```cpp
  #include "imu_driver.hpp"
  ```

- [ ] **Step 5: Add `imu_events_init()` call in `app_main()`**

  After `bsp_bus_init()` and before any `xTaskCreate` calls, add:
  ```cpp
  /* Create IMU system event group before IMU tasks start */
  imu_events_init();
  ```

- [ ] **Step 6: Add `xTaskCreatePinnedToCore` calls after the radar task creation**

  ```cpp
  #define IMU_DATA_TASK_STACK      8192
  #define IMU_DATA_TASK_PRIORITY      4
  #define IMU_MOTION_TASK_STACK    4096
  #define IMU_MOTION_TASK_PRIORITY    3

  BaseType_t imu_data_ret = xTaskCreatePinnedToCore(
      imu_data_task, "imu_data",
      IMU_DATA_TASK_STACK, NULL, IMU_DATA_TASK_PRIORITY, NULL, 1);
  if (imu_data_ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create imu_data_task");
  } else {
      ESP_LOGI(TAG, "IMU data task started");
  }

  BaseType_t imu_motion_ret = xTaskCreatePinnedToCore(
      imu_motion_task, "imu_motion",
      IMU_MOTION_TASK_STACK, NULL, IMU_MOTION_TASK_PRIORITY, NULL, 1);
  if (imu_motion_ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create imu_motion_task");
  } else {
      ESP_LOGI(TAG, "IMU motion task started");
  }
  ```

- [ ] **Step 7: Full build**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py build 2>&1 | grep -E "error:|warning:" | head -40
  ```
  Expected: zero errors.

- [ ] **Step 8: Commit**

  ```bash
  git add main/app_main.cpp main/CMakeLists.txt
  git commit -m "feat(main): rename to cpp, wire imu_data_task and imu_motion_task into app_main"
  ```

---

## Task 9: Flash and verify on hardware

Confirm both tasks start, sensor data flows, and motion detection fires.

- [ ] **Step 1: Flash and open monitor**

  ```bash
  cd /Users/willmain/devPetPulse/dev/PetPulse/firmware
  idf.py flash monitor
  ```

- [ ] **Step 2: Verify startup sequence in serial output**

  Expected log lines (after BLE phone connects + time syncs):
  ```
  I (IMU_Data) Waiting for BLE auth + time sync...
  I (IMU_Data) BLE ready — starting IMU data collection
  I (IMU_Motion) Significant motion detector armed
  ```

- [ ] **Step 3: Verify Background mode sensor data (step + activity callbacks firing every ~10s)**

  Watch for step_counter and activity_classifier updates in the BLE characteristic (use nRF Connect or similar to subscribe to `C_ACTIVITY_UUID`).

- [ ] **Step 4: Switch mode to Dev (via `C_MODE_UUID` write = 0x02) and verify raw motion callbacks firing**

  Watch for accel/gyro/magf/rv updates on `C_RAW_UUID` every ~1s.

- [ ] **Step 5: Move the device to trigger significant motion — verify log**

  ```
  I (IMU_Motion) Significant motion detected — re-arming
  ```
  Any task watching `imuEventGroup` for `IMU_EVT_MOTION_BIT` should also react.

- [ ] **Step 6: Disconnect BLE and verify SD stub is called (add temporary ESP_LOGI to `sd_write_aggregated_record` if needed)**

  ```cpp
  static esp_err_t sd_write_aggregated_record(const BleAggregatedAll_t &record) {
      ESP_LOGD("SD_STUB", "sd_write presence=0x%02x", record.presence_bitmask);
      return ESP_OK;
  }
  ```

---

## Notes for Implementor

- `imu.rpt.rv.get_quat()` — check `BNO08xRptRV.hpp` for the exact getter name; it may be `get()` returning a `bno08x_quat_t` directly.
- `bleServer.updateAccel()`, `updateGyro()`, `updateMagf()`, `updateRV()`, `updateStepCount()`, `updateActivityClass()` are all public methods on `BleServer` — see `ble_driver.hpp` lines 332–337.
- `bleEventGroup` is defined as `extern EventGroupHandle_t bleEventGroup` in `ble_driver.hpp` — accessible in `imu_driver.cpp` after `#include "ble_driver.hpp"`.
- The `BleRaw_t` and `BleActivity_t` member names on `BleAggregatedAll_t` are `raw` and `activity` — verify after struct refactor in Task 1.
- If `app_main.c` → `app_main.cpp` rename is needed, update `main/CMakeLists.txt` line that lists source files.
