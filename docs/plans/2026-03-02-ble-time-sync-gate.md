# BLE Time-Sync Event Group Gate Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Gate all IMU data transmission behind a FreeRTOS event group that requires both BLE authentication and a valid time-sync write before any sensor readings are sent to the mobile app.

**Architecture:** Add a `bleEventGroup` (FreeRTOS `EventGroupHandle_t`) to `ble_driver` with two bits: `BLE_AUTHENTICATED_BIT` and `BLE_TIME_SYNCED_BIT`. The BLE callbacks set/clear these bits. `data_processing_task` blocks at startup with `xEventGroupWaitBits` (both bits required), and the IMU callback uses a fast non-blocking `xEventGroupGetBits` check for reconnection gating — replacing the current `bleServer.isAuthenticated()` poll which only covers auth but never time sync.

**Tech Stack:** ESP-IDF v5.5+, NimBLE (esp-nimble-cpp v2.3.4), FreeRTOS (bundled with ESP-IDF), BNO08x IMU

---

## Context: The Bug

Between `onAuthenticationComplete()` and the mobile app writing Current Time (characteristic `2A2B`), there is a window — potentially several seconds — during which `data_processing_task` is already running and the IMU callback fires. Any readings during this window are timestamped with `getUTCTimestamp()`, which returns epoch-0 (1970-01-01 00:00:00.000) because `settimeofday()` hasn't been called yet. These corrupted readings get notified to the mobile app.

The fix: block the callback (and initial task setup) until both bits are set.

## Files Changed

| File | Change |
|---|---|
| `firmware/components/ble_driver/src/ble_driver.hpp` | Add `#include`, bit defines, `extern EventGroupHandle_t bleEventGroup` |
| `firmware/components/ble_driver/src/ble_driver.cpp` | Create group in `init()`, set/clear bits in callbacks and `syncSysTime()` |
| `firmware/components/imu_driver/src/imu_driver.cpp` | Add `xEventGroupWaitBits` at task start; replace `isAuthenticated()` in callback |

---

## Task 1: Declare the event group in `ble_driver.hpp`

**Files:**
- Modify: `firmware/components/ble_driver/src/ble_driver.hpp`

**Step 1: Add the FreeRTOS event group include and bit constants**

After the existing `#include <atomic>` line (currently line 5), add:

```cpp
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BLE_AUTHENTICATED_BIT  (1 << 0)
#define BLE_TIME_SYNCED_BIT    (1 << 1)
```

**Step 2: Declare the extern handle**

After the `extern BleServer bleServer;` line (currently line 332), add:

```cpp
extern EventGroupHandle_t bleEventGroup;
```

**Step 3: Verify the header compiles (no idf.py build needed — check for parse errors)**

```bash
cd firmware
idf.py reconfigure 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: no new errors. The reconfigure step regenerates `compile_commands.json` — this also fixes clangd after the `.clangd` `CompilationDatabase: build` change.

**Step 4: Commit**

```bash
git add firmware/components/ble_driver/src/ble_driver.hpp
git commit -m "feat(ble): declare BLE_AUTHENTICATED_BIT, BLE_TIME_SYNCED_BIT event group"
```

---

## Task 2: Create and manage the event group in `ble_driver.cpp`

**Files:**
- Modify: `firmware/components/ble_driver/src/ble_driver.cpp`

**Step 1: Define the global handle**

After the existing global definitions (after `CharacteristicCallbacks chrCallbacks;`, currently line 16), add:

```cpp
EventGroupHandle_t bleEventGroup = nullptr;
```

**Step 2: Create the event group in `BleServer::init()`**

Inside `BleServer::init()`, immediately after `NimBLEDevice::init(DEVICE_NAME);` (currently line 140), add:

```cpp
    bleEventGroup = xEventGroupCreate();
    if(bleEventGroup == nullptr) {
        ESP_LOGE(TAG, "Failed to create BLE event group");
        NimBLEDevice::deinit();
        return false;
    }
```

**Step 3: Set `BLE_AUTHENTICATED_BIT` in `onAuthenticationComplete()`**

The current body of `onAuthenticationComplete()` (line 86) calls `bleServer.setAuthenticated(true)`. Replace that one line with:

```cpp
    bleServer.setAuthenticated(true);
    xEventGroupSetBits(bleEventGroup, BLE_AUTHENTICATED_BIT);
```

Keep `setAuthenticated(true)` — `main.cpp` still polls `isAuthenticated()` to know when to spawn the task. The event group is additive.

**Step 4: Clear both bits in `onDisconnect()`**

The current body calls `bleServer.setAuthenticated(false)` (line 67). After that line, add:

```cpp
    xEventGroupClearBits(bleEventGroup, BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT);
```

This ensures a reconnecting client must re-auth AND re-sync time before data flows again.

**Step 5: Set `BLE_TIME_SYNCED_BIT` in `syncSysTime()`**

`syncSysTime()` returns `true` after calling `settimeofday()` (currently line 33). Before that `return true;`, add:

```cpp
    xEventGroupSetBits(bleEventGroup, BLE_TIME_SYNCED_BIT);
    return true;
```

**Step 6: Build to confirm no compile errors**

```bash
cd firmware
idf.py build 2>&1 | tail -20
```

Expected: `Build complete!` with no new errors.

**Step 7: Commit**

```bash
git add firmware/components/ble_driver/src/ble_driver.cpp
git commit -m "feat(ble): create event group, set/clear AUTHENTICATED and TIME_SYNCED bits"
```

---

## Task 3: Gate `data_processing_task` on both bits

**Files:**
- Modify: `firmware/components/imu_driver/src/imu_driver.cpp`

### Sub-task 3a: Block task startup until both bits are set

**Step 1: Add the wait at the very top of `data_processing_task`**

`data_processing_task` currently starts at line 585. The first thing it does is `bleServer.getMode()` and `configure_activity_reports()`. Before those lines, add:

```cpp
void data_processing_task(void *pvParameters) {
    // Block until the mobile app has both authenticated AND written the current time.
    // This prevents epoch-0 timestamps from being sent during the auth→time-sync window.
    xEventGroupWaitBits(bleEventGroup,
                        BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT,
                        pdFALSE,    // do not clear bits on exit
                        pdTRUE,     // wait for ALL bits
                        portMAX_DELAY);

    Mode active_mode = bleServer.getMode();
    configure_activity_reports(active_mode);
    // ... rest of function unchanged
```

Since `main.cpp` already waits for `isAuthenticated()` before spawning this task, `BLE_AUTHENTICATED_BIT` will usually already be set when the task starts — the wait here primarily catches `BLE_TIME_SYNCED_BIT`.

### Sub-task 3b: Replace `isAuthenticated()` in the IMU callback

**Step 2: Update the callback gate**

Inside `data_processing_task`, the lambda registered with `imu.register_cb(...)` currently starts with:

```cpp
        if(!bleServer.isAuthenticated()) return;
```

Replace that line with a non-blocking event group check (callbacks must not block):

```cpp
        if((xEventGroupGetBits(bleEventGroup) & (BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT))
                != (BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT)) return;
```

This gates every individual reading on reconnection (if the client disconnects and reconnects, bits are cleared by `onDisconnect()` and the callback silently skips until both are re-established).

**Step 3: Build**

```bash
cd firmware
idf.py build 2>&1 | tail -20
```

Expected: `Build complete!`

**Step 4: Commit**

```bash
git add firmware/components/imu_driver/src/imu_driver.cpp
git commit -m "feat(imu): gate data_processing_task on AUTHENTICATED + TIME_SYNCED event bits"
```

---

## Task 4: Flash and verify on device

**Step 1: Flash**

```bash
cd firmware
idf.py flash monitor
```

**Step 2: Connect mobile app — verify the expected log sequence**

Watch the serial monitor. The correct order must be:

```
I (BLE) Authentication successful bonded: true        ← AUTHENTICATED_BIT set
I (BLE) System time set to: 2026-03-02 19:43:38       ← TIME_SYNCED_BIT set
I (IMU_DRIVER) Activity reports enabled: mode=0, ...  ← task unblocks HERE
I (IMU_DRIVER) Step Counter: 0                        ← first data (timestamped correctly)
```

**Step 3: Verify no epoch-0 timestamps appear**

In the mobile app logs, every `stepCount` and `activityClass` event must have a timestamp ≥ the time-sync write. If you see `1970-01-01` timestamps, the gate is not working — check that `syncSysTime()` is being called (look for the `System time set to:` log line).

**Step 4: Verify reconnection gating**

1. Disconnect the mobile app
2. Reconnect without writing time (if possible to test)
3. Confirm no data flows until the time characteristic is written again

**Step 5: Final commit (if any fixups were needed)**

```bash
git add -p
git commit -m "fix(ble): <description of any fixup>"
```

---

## Bit Lifecycle Summary

```
Event          │ AUTHENTICATED_BIT │ TIME_SYNCED_BIT
───────────────┼───────────────────┼────────────────
Startup        │        0          │       0
onConnect      │        0          │       0         (no change)
onAuthComplete │        1          │       0
Time write     │        1          │       1         → task/callback unblocked
onDisconnect   │        0          │       0         → callback gated again
Reconnect auth │        1          │       0
Reconnect time │        1          │       1         → unblocked again
```
