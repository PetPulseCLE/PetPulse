/**
 * @file findmy.c
 * @brief Apple Find My Network beacon — ESP32-S3 NimBLE implementation.
 *
 * Ported from the Flipper Zero "FindMy Flipper" app.  Stripped to Apple-only
 * (Samsung SmartTag and Tile removed) and rewritten to use NimBLE directly
 * instead of the Flipper HAL "extra beacon" API.
 *
 * == How it works ==
 *
 * The Apple Offline Finding protocol broadcasts a 31-byte manufacturer-specific
 * advertisement containing 22 bytes of an EC public key (SECP224R1 x-coordinate).
 * The BLE address is derived from the first 6 bytes of that key with the top
 * two bits set to 0b11 (random static address type).
 *
 * iPhones running iOS 14.5+ detect these beacons and relay encrypted location
 * reports to Apple's servers.  The owner (who holds the private key) can decrypt
 * them in the Find My app.
 *
 * == Advertising coexistence ==
 *
 * TIME-DIVISION MULTIPLEXING with the main PetPulse advertising:
 *
 *   Disconnected: PetPulse connectable runs normally.  Every interval_s seconds,
 *                 a FreeRTOS timer stops PetPulse, broadcasts FindMy for 2 seconds,
 *                 then restarts PetPulse.  The 2-second gap is short enough that
 *                 the owner's phone can still reconnect on the next cycle.
 *
 *   Connected:    PetPulse doesn't need to advertise (connection is established).
 *                 FindMy runs continuously on the freed advertising slot.
 *                 This is ideal — the Apple network builds location history
 *                 even when the pet is at home.
 *
 * == Thread safety ==
 *
 * All NimBLE ble_gap_* functions acquire the host lock internally, so they're
 * safe to call from the FreeRTOS timer task.  The findmy_on_ble_event() callback
 * runs from the NimBLE host task (via ble_fire_event → app_main callback),
 * which is also safe due to the internal locking.
 */

#include "findmy.h"
#include "findmy_keys.h"
#include "bms_driver.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// NimBLE — direct API access for non-connectable advertising
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#define TAG "FindMy"

// ==========================================================================
// Apple Offline Finding Constants
// ==========================================================================

/** Apple Company ID (little-endian in BLE) */
#define APPLE_COMPANY_ID_LO     0x4C
#define APPLE_COMPANY_ID_HI     0x00

/** Apple Offline Finding type byte */
#define APPLE_OF_TYPE           0x12

/** Offline Finding payload length (after type byte) */
#define APPLE_OF_LEN            0x19

/** Total raw advertising data length */
// Defined in findmy_keys.h: #define FINDMY_PAYLOAD_LEN      31

/** Apple-recommended advertising interval for accessories: 152.5 ms */
#define FINDMY_ADV_ITVL         244     // 244 × 0.625 ms = 152.5 ms

/** Battery level encoding in Apple's format */
#define BATTERY_FULL            0x00
#define BATTERY_MEDIUM          0x50
#define BATTERY_LOW             0xA0
#define BATTERY_CRITICAL        0xF0

// ==========================================================================
// Internal State Machine
// ==========================================================================

typedef enum {
    FM_IDLE,                      /**< Not running */
    FM_INTERLEAVE_PETPULSE,       /**< PetPulse is advertising; FindMy waiting for timer */
    FM_INTERLEAVE_FINDMY,         /**< FindMy burst active (2 s) */
    FM_CONTINUOUS,                /**< Phone connected; FindMy runs non-stop */
} fm_state_t;

/*
 * Worker task notification bits. The FreeRTOS timer callback must not
 * acquire the NimBLE host lock (Tmr Svc has a tight 2 KB stack and the
 * task is shared with other timers), so it only sets a notification and
 * returns. The dedicated worker task owns a 4 KB stack and does the
 * actual NimBLE calls.
 */
#define FM_NOTIFY_BURST     (1u << 0)
#define FM_NOTIFY_STOP      (1u << 1)

#define FM_WORKER_STACK     4096
#define FM_WORKER_PRIORITY  4

static struct {
    bool            key_valid;
    fm_state_t      state;
    uint8_t         interval_s;
    uint8_t         addr[6];                    /**< Random static address (LE byte order) */
    uint8_t         payload[FINDMY_PAYLOAD_LEN]; /**< Raw advertising data */
    TimerHandle_t   timer;                       /**< Interleave timer (one-shot) */
    TaskHandle_t    worker;                      /**< Worker task (owns NimBLE calls) */
} s_fm;

// Forward declarations
static void fm_timer_cb(TimerHandle_t timer);
static void fm_worker_task(void *arg);
static int  fm_gap_event(struct ble_gap_event *event, void *arg);

// ==========================================================================
// Payload Construction
// ==========================================================================

/**
 * Build the BLE address and 31-byte Apple Offline Finding payload from
 * a 28-byte SECP224R1 public key (x-coordinate).
 *
 * Payload layout (identical to the Flipper Zero implementation):
 *   [0]     0x1E          AD Length (30)
 *   [1]     0xFF          AD Type: Manufacturer Specific Data
 *   [2-3]   0x4C 0x00     Apple Company ID
 *   [4]     0x12          Offline Finding type
 *   [5]     0x19          Offline Finding data length (25)
 *   [6]     battery       Battery status byte
 *   [7-28]  key[6..27]    Public key bytes 6-27 (22 bytes)
 *   [29]    key[0] >> 6   Top 2 bits of key[0]
 *   [30]    0x00          Hint byte
 *
 * BLE address:
 *   addr[0:6] = key[0:6], addr[0] |= 0xC0 (random static)
 *   Stored little-endian (reversed) for NimBLE.
 */
static void fm_build(const uint8_t mac[FINDMY_MAC_LEN], const uint8_t payload[FINDMY_PAYLOAD_LEN])
{
    // ── Load BLE address ─────────────────────────────────────────────
    // MAC address from nRF Connect is in big-endian (e.g. AA:BB:CC:DD:EE:FF).
    // NimBLE requires little-endian, so we reverse it.
    for (int i = 0; i < 6; i++) {
        s_fm.addr[i] = mac[5 - i];
    }

    // ── Load advertisement payload ───────────────────────────────────
    memcpy(s_fm.payload, payload, FINDMY_PAYLOAD_LEN);
}

// ==========================================================================
// Low-Level Advertising Control
// ==========================================================================

/**
 * Start FindMy non-connectable advertising.
 * @param duration_ms  Duration before auto-stop, or BLE_HS_FOREVER.
 */
static void fm_adv_start(int32_t duration_ms)
{
    // Set the FindMy-derived random static address
    int rc = ble_hs_id_set_rnd(s_fm.addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_set_rnd failed: %d", rc);
        return;
    }

    // Set raw advertising data — the full 31-byte Apple payload
    rc = ble_gap_adv_set_data(s_fm.payload, FINDMY_PAYLOAD_LEN);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data failed: %d", rc);
        return;
    }

    // Non-connectable, non-discoverable (per Apple's spec)
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    params.itvl_min  = FINDMY_ADV_ITVL;
    params.itvl_max  = FINDMY_ADV_ITVL;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, duration_ms,
                           &params, fm_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGD(TAG, "FindMy adv started (dur=%d ms)", (int)duration_ms);
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "FindMy adv_start failed: %d", rc);
    }
}

static void fm_adv_stop(void)
{
    ble_gap_adv_stop();
}

// ==========================================================================
// GAP Event Callback (for FindMy's own advertising)
// ==========================================================================

/**
 * When FindMy starts advertising with ble_gap_adv_start(), this callback
 * receives GAP events for that advertising set.  The only event we care
 * about is ADV_COMPLETE (timed advertising expired).
 */
static int fm_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        ESP_LOGD(TAG, "FindMy burst complete");

        if (s_fm.state == FM_INTERLEAVE_FINDMY) {
            // Burst done — restore PetPulse connectable advertising
            s_fm.state = FM_INTERLEAVE_PETPULSE;
            ble_driver_start_advertising();

            // Re-arm timer for next FindMy burst
            if (s_fm.timer) {
                xTimerChangePeriod(s_fm.timer,
                    pdMS_TO_TICKS((uint32_t)s_fm.interval_s * 1000),
                    0);
                xTimerStart(s_fm.timer, 0);
            }
        }
    }

    return 0;
}

// ==========================================================================
// Timer Callback (initiates each FindMy burst)
// ==========================================================================

/**
 * Timer callback — runs on the shared FreeRTOS "Tmr Svc" task (2 KB stack).
 *
 * Must stay short and must NOT call into NimBLE: the host lock plus
 * stack-allocated NimBLE structs can overflow Tmr Svc. The callback only
 * notifies the dedicated worker, which owns a 4 KB stack.
 */
static void fm_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (s_fm.worker) {
        xTaskNotify(s_fm.worker, FM_NOTIFY_BURST, eSetBits);
    }
}

/**
 * Worker task — owns all NimBLE calls triggered by the interleave timer.
 *
 * Sleeps on a task notification; wakes to either run one FindMy burst or
 * exit (on FM_NOTIFY_STOP from findmy_deinit).
 */
static void fm_worker_task(void *arg)
{
    (void)arg;

    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);

        if (bits & FM_NOTIFY_STOP) {
            break;
        }

        if ((bits & FM_NOTIFY_BURST) == 0) {
            continue;
        }

        /* State may have changed (connect/disconnect) between timer fire
         * and worker wake — re-check under the current state. */
        if (s_fm.state != FM_INTERLEAVE_PETPULSE) {
            continue;
        }

        /* Refresh battery level before broadcasting */
        power_snapshot_t snap;
        if (power_manager_get_snapshot(&snap)) {
            findmy_update_battery(snap.soc_pct);
        }

        /* Stop PetPulse advertising — frees the advertising slot */
        ble_driver_stop_advertising();

        /* Start timed FindMy burst. The controller auto-stops after
         * FINDMY_BURST_DURATION_MS, which fires BLE_GAP_EVENT_ADV_COMPLETE
         * in fm_gap_event (on the NimBLE host task) — that handler
         * restores PetPulse and re-arms this timer. */
        s_fm.state = FM_INTERLEAVE_FINDMY;
        fm_adv_start(FINDMY_BURST_DURATION_MS);
    }

    s_fm.worker = NULL;
    vTaskDelete(NULL);
}

// ==========================================================================
// Public API
// ==========================================================================

esp_err_t findmy_init(void)
{
    memset(&s_fm, 0, sizeof(s_fm));
    s_fm.state      = FM_IDLE;
    s_fm.interval_s = FINDMY_DEFAULT_INTERVAL_S;

    // Load the Find My data (NVS → compiled default → none)
    uint8_t mac[FINDMY_MAC_LEN];
    uint8_t payload[FINDMY_PAYLOAD_LEN];
    if (!findmy_data_load(mac, payload)) {
        ESP_LOGW(TAG, "No valid MAC/Payload — FindMy beacon disabled");
        return ESP_ERR_NOT_FOUND;
    }

    // Build BLE address + Apple payload
    fm_build(mac, payload);
    s_fm.key_valid = true;

    // Create the one-shot interleave timer
    s_fm.timer = xTimerCreate(
        "findmy",
        pdMS_TO_TICKS((uint32_t)s_fm.interval_s * 1000),
        pdFALSE,            // One-shot — re-armed in fm_gap_event
        NULL,
        fm_timer_cb);

    if (!s_fm.timer) {
        ESP_LOGE(TAG, "Timer create failed");
        return ESP_FAIL;
    }

    // Spawn the worker task before arming the timer so a fast-firing
    // notification always has a receiver.
    BaseType_t ok = xTaskCreate(fm_worker_task, "findmy_worker",
                                FM_WORKER_STACK, NULL,
                                FM_WORKER_PRIORITY, &s_fm.worker);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Worker task create failed");
        xTimerDelete(s_fm.timer, 0);
        s_fm.timer = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Apple Find My initialized (interval=%ds, burst=%dms)",
             s_fm.interval_s, FINDMY_BURST_DURATION_MS);

    return findmy_start();
}

void findmy_deinit(void)
{
    findmy_stop();

    if (s_fm.timer) {
        xTimerDelete(s_fm.timer, portMAX_DELAY);
        s_fm.timer = NULL;
    }

    // Signal the worker to exit and wait for it to self-delete before
    // zeroing s_fm, so we don't race the worker's final store.
    if (s_fm.worker) {
        TaskHandle_t w = s_fm.worker;
        xTaskNotify(w, FM_NOTIFY_STOP, eSetBits);
        // Poll briefly — the worker sets s_fm.worker = NULL then
        // vTaskDelete(NULL). 50 ms is generous; the task's only work
        // here is returning from xTaskNotifyWait.
        for (int i = 0; i < 10 && s_fm.worker != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    memset(&s_fm, 0, sizeof(s_fm));
    ESP_LOGI(TAG, "FindMy deinitialized");
}

bool findmy_is_active(void)
{
    return s_fm.state != FM_IDLE;
}

esp_err_t findmy_start(void)
{
    if (!s_fm.key_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_driver_is_connected()) {
        //
// Phone already connected — PetPulse advertising is off.
// Run FindMy continuously on the freed slot.
//
        s_fm.state = FM_CONTINUOUS;
        fm_adv_start(BLE_HS_FOREVER);
    } else {
        //
// No connection — PetPulse is advertising.
// Start the interleave timer; first FindMy burst fires in interval_s.
//
        s_fm.state = FM_INTERLEAVE_PETPULSE;
        xTimerStart(s_fm.timer, 0);
    }

    ESP_LOGI(TAG, "FindMy beacon active");
    return ESP_OK;
}

void findmy_stop(void)
{
    if (s_fm.timer) {
        xTimerStop(s_fm.timer, 0);
    }

    if (s_fm.state == FM_INTERLEAVE_FINDMY ||
        s_fm.state == FM_CONTINUOUS) {
        fm_adv_stop();
    }

    s_fm.state = FM_IDLE;
    ESP_LOGI(TAG, "FindMy beacon stopped");
}

void findmy_set_interval(uint8_t interval_s)
{
    if (interval_s < 1)  interval_s = 1;
    if (interval_s > 10) interval_s = 10;
    s_fm.interval_s = interval_s;
    ESP_LOGI(TAG, "FindMy interval set to %ds", interval_s);
}

esp_err_t findmy_reload_data(void)
{
    findmy_stop();

    uint8_t mac[FINDMY_MAC_LEN];
    uint8_t payload[FINDMY_PAYLOAD_LEN];
    if (!findmy_data_load(mac, payload)) {
        s_fm.key_valid = false;
        return ESP_ERR_NOT_FOUND;
    }

    fm_build(mac, payload);
    s_fm.key_valid = true;

    return findmy_start();
}

void findmy_update_battery(uint8_t soc_pct)
{
    uint8_t level;
    if (soc_pct > 80) {
        level = BATTERY_FULL;
    } else if (soc_pct > 50) {
        level = BATTERY_MEDIUM;
    } else if (soc_pct > 20) {
        level = BATTERY_LOW;
    } else {
        level = BATTERY_CRITICAL;
    }
    s_fm.payload[6] = level;
}

void findmy_on_ble_event(const ble_event_t *event)
{
    if (!s_fm.key_valid || s_fm.state == FM_IDLE) {
        return;
    }

    switch (event->type) {

    case BLE_EVENT_CONNECTED:
        //
// Phone connected — PetPulse advertising just stopped.
// Stop the interleave timer and switch to continuous FindMy.
//
        if (s_fm.timer) {
            xTimerStop(s_fm.timer, 0);
        }
        if (s_fm.state == FM_INTERLEAVE_FINDMY) {
            fm_adv_stop();   // Abort mid-burst if active
        }
        s_fm.state = FM_CONTINUOUS;
        fm_adv_start(BLE_HS_FOREVER);
        break;

    case BLE_EVENT_DISCONNECTED:
        //
// Phone disconnected — the BLE driver's disconnect handler has
// already restarted PetPulse connectable advertising.
// Stop continuous FindMy and switch to interleaved mode.
//
        fm_adv_stop();
        s_fm.state = FM_INTERLEAVE_PETPULSE;

        //
// The BLE driver's ble_start_adv_internal() may have failed
// (EALREADY) if FindMy was still running.  Now that FindMy is
// stopped, explicitly restart PetPulse.
//
        ble_driver_start_advertising();

        // Start the interleave timer
        if (s_fm.timer) {
            xTimerChangePeriod(s_fm.timer,
                pdMS_TO_TICKS((uint32_t)s_fm.interval_s * 1000), 0);
            xTimerStart(s_fm.timer, 0);
        }
        break;

    default:
        break;
    }
}
