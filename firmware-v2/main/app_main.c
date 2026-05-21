#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h" // Board Support Package
#include "button_driver.h" // Button UI driver
#include "nfc_driver.h" // NFC Driver
#include "nfc_pairing.h" // NFC Tap-to-Pair
#include "ble_driver.h" // BLE Driver
#include "findmy.h" // Apple Find My beacon
#include "findmy_keys.h" // Find My key storage (for factory reset)
#include "ws2812b.h" // RGB status LED
#include "led_manager.h" // System-wide LED feedback manager
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_rss_a121.h"
#include "acc_sensor.h"
#include "acc_version.h"

#include "a121_radar_driver.h"
#include "radar_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rtc_driver.h"
#include "bms_driver.h"
#include "imu_driver.h"
#include "env_driver.h"

static const char *TAG = "Main";

#define RADAR_TASK_STACK_SIZE  8192
#define RADAR_TASK_PRIORITY    5

#define IMU_DATA_TASK_STACK       8192
#define IMU_DATA_TASK_PRIORITY    4
#define IMU_MOTION_TASK_STACK     4096
#define IMU_MOTION_TASK_PRIORITY  3
#define IMU_TASK_CORE             1

#define ENV_SAMPLE_PERIOD_S       30

/* Radar → BLE vitals publishing.
 *
 * The vitals characteristic carries both heart rate and breathing rate in a
 * single packet, so we only push when BOTH measurements are available. HR
 * updates on every frame where confidence clears the threshold; BR only
 * lands when the estimator signals result_ready (several seconds apart),
 * so the latest of each is cached and paired at publish time.
 *
 * FRESHNESS_MS bounds how stale a cached side can be before we stop
 * pairing it with a new sample from the other side — a BR from 30 s ago
 * is not meaningful context for a current HR. MIN_PUBLISH_GAP_MS keeps
 * the BLE notify rate bounded while HR updates quickly. */
#define RADAR_HR_CONFIDENCE_THRESHOLD   50.0f
#define RADAR_VITALS_FRESHNESS_MS       15000
#define RADAR_VITALS_MIN_PUBLISH_GAP_MS 1000

/* Button UI */
#define BTN_LED_COUNT                 1
#define BTN_STATUS_PULSE_MS           250
#define BTN_CONFIRM_WINDOW_MS         5000
#define BTN_ARM_FLASH_MS              80

static bool s_led_ready = false;
static bool s_findmy_user_enabled = true;   /* User-level FindMy toggle (via DOUBLE_CLICK) */

/* Button-driven LED helpers. These forward to the LED manager's OVERRIDE
 * layer — the manager's BASE and OVERLAY are ignored while override is set,
 * so gesture feedback always wins.  The vTaskDelay in led_flash() preserves
 * the historical "fill for N ms then restore" semantics; when override is
 * released the manager resumes whatever base state is currently active
 * (charging, BLE adv, etc.) on its next render tick. */
static void led_flash(ws2812b_color_t c, uint32_t on_ms)
{
    if (!s_led_ready) return;
    led_manager_override_steady(c);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_manager_override_release();
}

static void led_solid(ws2812b_color_t c)
{
    if (!s_led_ready) return;
    led_manager_override_steady(c);
}

static void led_off(void)
{
    if (!s_led_ready) return;
    led_manager_override_release();
}

/* Map current battery + BLE state to a single status colour. */
static ws2812b_color_t status_color(void)
{
    uint8_t pct = power_manager_get_pct();
    if (pct <= BMS_CRITICAL_BATTERY_PCT) return WS2812B_COLOR_RED;
    if (pct <= BMS_LOW_BATTERY_PCT)      return WS2812B_COLOR_AMBER;
    if (power_manager_is_charging())     return WS2812B_COLOR_COOL_WHITE;
    if (ble_driver_is_connected())       return WS2812B_COLOR_GREEN;
    return WS2812B_COLOR_BLUE;
}

/* Confirmation callbacks for the two-stage destructive gestures. */
static void power_off_confirm_cb(bool confirmed, void *ud)
{
    (void)ud;
    if (!confirmed) {
        ESP_LOGI(TAG, "Power-off cancelled");
        led_flash(WS2812B_COLOR_BLUE, 120);
        return;
    }
    ESP_LOGW(TAG, "Power-off confirmed — shutting down");
    for (int i = 0; i < 2; i++) {
        led_flash(WS2812B_COLOR_RED, 180);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ble_driver_stop_advertising();
    power_manager_off();
}

static void factory_reset_confirm_cb(bool confirmed, void *ud)
{
    (void)ud;
    if (!confirmed) {
        ESP_LOGI(TAG, "Factory reset cancelled");
        led_flash(WS2812B_COLOR_BLUE, 120);
        return;
    }
    ESP_LOGW(TAG, "Factory reset confirmed — erasing bonds + FindMy data, rebooting");
    for (int i = 0; i < 3; i++) {
        led_flash(WS2812B_COLOR_RED, 140);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ble_driver_clear_bonds();
    findmy_data_erase();
    vTaskDelay(pdMS_TO_TICKS(200));     /* Let NVS commits land */
    power_manager_reset();
}

/* Main button dispatcher. Runs on the button driver task. */
static void button_event_handler(const button_event_t *ev, void *user_data)
{
    (void)user_data;

    switch (ev->type) {
    case BUTTON_EVENT_SINGLE_CLICK:
        led_flash(status_color(), BTN_STATUS_PULSE_MS);
        ESP_LOGI(TAG, "Status: %d%% battery, BLE %s, charging=%d",
                 power_manager_get_pct(),
                 ble_driver_is_connected() ? "connected" : "disconnected",
                 (int)power_manager_is_charging());
        break;

    case BUTTON_EVENT_DOUBLE_CLICK:
        s_findmy_user_enabled = !s_findmy_user_enabled;
        if (s_findmy_user_enabled) {
            findmy_start();
            ESP_LOGI(TAG, "FindMy beacon: ON");
            led_flash(WS2812B_COLOR_GREEN, 250);
        } else {
            findmy_stop();
            ESP_LOGI(TAG, "FindMy beacon: OFF");
            led_flash(WS2812B_COLOR_AMBER, 250);
        }
        break;

    case BUTTON_EVENT_TRIPLE_CLICK:
        if (ble_driver_is_connected()) {
            ESP_LOGI(TAG, "Pairing requested but already connected — ignoring");
            led_flash(WS2812B_COLOR_BLUE, 200);
        } else {
            ESP_LOGI(TAG, "Entering pairing mode — BLE advertising + NFC NDEF refresh");
            ble_driver_start_advertising();
            nfc_pairing_refresh_ndef();
            led_flash(WS2812B_COLOR_CYAN, 400);
        }
        break;

    case BUTTON_EVENT_QUAD_CLICK: {
        ble_mode_t m = ble_driver_get_mode();
        ble_mode_t next = (m == BLE_MODE_LIVE) ? BLE_MODE_BACKGROUND : BLE_MODE_LIVE;
        ble_driver_set_mode(next);
        ESP_LOGI(TAG, "BLE mode -> %s",
                 next == BLE_MODE_LIVE ? "LIVE" : "BACKGROUND");
        led_flash(next == BLE_MODE_LIVE ? WS2812B_COLOR_CYAN : WS2812B_COLOR_INDIGO, 250);
        break;
    }

    case BUTTON_EVENT_QUINT_CLICK:
        /* Stage 1 of factory-reset gesture — arm window for the hold confirm. */
        ESP_LOGW(TAG, "Factory-reset armed — repeat 5×click+hold within %d ms to confirm",
                 BTN_CONFIRM_WINDOW_MS);
        button_driver_arm_confirmation(BUTTON_EVENT_QUINT_CLICK_HOLD,
                                        BTN_CONFIRM_WINDOW_MS,
                                        factory_reset_confirm_cb, NULL);
        for (int i = 0; i < 3; i++) {
            led_flash(WS2812B_COLOR_RED, BTN_ARM_FLASH_MS);
            vTaskDelay(pdMS_TO_TICKS(BTN_ARM_FLASH_MS));
        }
        break;

    case BUTTON_EVENT_LONG_PRESS_START:
        led_solid(WS2812B_COLOR_YELLOW);
        break;

    case BUTTON_EVENT_LONG_PRESS_HOLD: {
        /* Breath toward orange as we approach VERY_LONG_PRESS. */
        uint32_t num = ev->held_ms * 255U;
        uint32_t b   = num / BUTTON_VERY_LONG_PRESS_MS;
        if (b > 255) b = 255;
        led_solid(ws2812b_color_brightness(WS2812B_COLOR_ORANGE, (uint8_t)b));
        break;
    }

    case BUTTON_EVENT_VERY_LONG_PRESS:
        led_solid(WS2812B_COLOR_ORANGE);
        ESP_LOGW(TAG, "Power-off armed — release + double-click+hold within %d ms to confirm",
                 BTN_CONFIRM_WINDOW_MS);
        button_driver_arm_confirmation(BUTTON_EVENT_DOUBLE_CLICK_HOLD,
                                        BTN_CONFIRM_WINDOW_MS,
                                        power_off_confirm_cb, NULL);
        break;

    case BUTTON_EVENT_LONG_PRESS_RELEASE:
        led_off();
        break;

    case BUTTON_EVENT_CLICK_HOLD:
        /* Reserved for snooze/ack. Brief cyan acknowledge for now. */
        led_flash(WS2812B_COLOR_CYAN, 150);
        break;

    case BUTTON_EVENT_TRIPLE_CLICK_HOLD:
        /* SOS gesture: forces LIVE mode (dense telemetry) + strobe. */
        ESP_LOGW(TAG, "SOS gesture — switching BLE to LIVE mode");
        ble_driver_set_mode(BLE_MODE_LIVE);
        if (!ble_driver_is_advertising() && !ble_driver_is_connected()) {
            ble_driver_start_advertising();
        }
        for (int i = 0; i < 8; i++) {
            led_flash(WS2812B_COLOR_RED, 90);
            led_flash(WS2812B_COLOR_WHITE, 90);
        }
        led_off();
        break;

    case BUTTON_EVENT_STUCK:
        ESP_LOGW(TAG, "Button stuck (held >%d ms)", BUTTON_STUCK_THRESHOLD_MS);
        led_solid(WS2812B_COLOR_RED);
        break;

    case BUTTON_EVENT_UNSTUCK:
        ESP_LOGI(TAG, "Button released from stuck state");
        led_off();
        break;

    case BUTTON_EVENT_HARD_RESET_IMMINENT:
        ESP_LOGE(TAG, "HARD RESET IMMINENT — QON will trip in ~%d ms",
                 BUTTON_QON_RESET_MS - BUTTON_HARD_RESET_WARN_MS);
        /* Do only what is cheap & synchronous before the board dies. */
        ble_driver_stop_advertising();
        led_solid(ws2812b_color_brightness(WS2812B_COLOR_RED, 32));
        break;

    case BUTTON_EVENT_STUCK_AT_BOOT:
        ESP_LOGW(TAG, "Button was asserted at boot — possibly pinned or prior QON reset");
        led_flash(WS2812B_COLOR_RED, 200);
        break;

    case BUTTON_EVENT_LOCKED_ACTIVITY:
        /* Short violet blink — reminds the user the button is disabled (e.g. on charger). */
        led_flash(WS2812B_COLOR_VIOLET, 120);
        break;

    default:
        break;
    }
}

/* BLE event callback — logs connection/pairing events and drives FindMy */
static void ble_event_handler(const ble_event_t *event, void *user_data)
{
    (void)user_data;

    /* Fan out to the LED manager first so visual feedback lands even if
     * the logging below adds latency. */
    led_manager_on_ble_event(event);

    switch (event->type) {
    case BLE_EVENT_CONNECTED:
        ESP_LOGI(TAG, "BLE: Phone connected (handle=%d, addr=%02X:%02X:%02X:%02X:%02X:%02X)",
                 event->conn_handle,
                 event->connected.peer_addr[5], event->connected.peer_addr[4],
                 event->connected.peer_addr[3], event->connected.peer_addr[2],
                 event->connected.peer_addr[1], event->connected.peer_addr[0]);
        break;
    case BLE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "BLE: Phone disconnected (reason=0x%02x)", event->disconnected.reason);
        break;
    case BLE_EVENT_PAIRED:
        ESP_LOGI(TAG, "BLE: Pairing complete (bonded=%d, encrypted=%d)",
                 event->paired.bonded, event->paired.encrypted);
        break;
    case BLE_EVENT_PAIRING_FAILED:
        ESP_LOGW(TAG, "BLE: Pairing failed");
        break;
    case BLE_EVENT_MTU_CHANGED:
        ESP_LOGI(TAG, "BLE: MTU changed to %d", event->mtu_changed.mtu);
        break;
    default:
        break;
    }

    /* Forward connect/disconnect to FindMy so it can switch between
     * interleaved (disconnected) and continuous (connected) modes. */
    findmy_on_ble_event(event);
}

/* Push the current power snapshot to all BLE battery characteristics
 * (legacy 0x2A19 SOC% and the BAS 1.1 extended 0x2BED/0x2BF0/0x2BEE/0x2BEA
 * set) and refresh the FindMy battery nibble.
 *
 * Called from every BMS event that changes battery or charger state.
 * The snapshot's charger-derived fields (usb_connected, charge_status)
 * are refreshed synchronously by the BMS monitor *before* the event
 * fires, so this sees fresh state even on USB/charge transitions that
 * occur between 30s gauge polls. */
static void push_bas_to_ble(void)
{
    power_snapshot_t snap;
    if (!power_manager_get_snapshot(&snap)) {
        return;
    }
    ble_driver_notify_battery_level(snap.soc_pct);
    findmy_update_battery(snap.soc_pct);
    ble_driver_sync_extended_battery(&snap);
}

/* Power management event callback — logs BMS events */
static void power_event_handler(const power_event_t *event, void *user_data)
{
    (void)user_data;

    led_manager_on_power_event(event);

    switch (event->type) {
    case POWER_EVENT_USB_CONNECTED:
        ESP_LOGI(TAG, "BMS: USB connected");
        /* Disable user gestures while docked so the collar can't be
         * powered off or factory-reset by being set down on the charger. */
        button_driver_lock();
        push_bas_to_ble();
        break;
    case POWER_EVENT_USB_DISCONNECTED:
        ESP_LOGI(TAG, "BMS: USB disconnected");
        button_driver_unlock();
        push_bas_to_ble();
        break;
    case POWER_EVENT_CHARGE_START:
        ESP_LOGI(TAG, "BMS: Charging started");
        push_bas_to_ble();
        break;
    case POWER_EVENT_CHARGE_DONE:
        ESP_LOGI(TAG, "BMS: Charge complete");
        push_bas_to_ble();
        break;
    case POWER_EVENT_CHARGE_STOPPED:
        ESP_LOGW(TAG, "BMS: Charging stopped");
        push_bas_to_ble();
        break;
    case POWER_EVENT_LOW_BATTERY:
        ESP_LOGW(TAG, "BMS: Low battery (%d%%, %dmV)",
                 event->battery.soc_pct, event->battery.voltage_mv);
        push_bas_to_ble();
        break;
    case POWER_EVENT_CRITICAL_BATTERY:
        ESP_LOGE(TAG, "BMS: CRITICAL battery (%d%%, %dmV)",
                 event->battery.soc_pct, event->battery.voltage_mv);
        push_bas_to_ble();
        break;
    case POWER_EVENT_CHARGER_FAULT:
        ESP_LOGE(TAG, "BMS: Charger fault (chrg=%d ntc=%d bat=%d wd=%d)",
                 event->fault.chrg_fault, event->fault.ntc_fault,
                 event->fault.bat_fault, event->fault.watchdog_fault);
        push_bas_to_ble();
        break;
    case POWER_EVENT_SOURCE_DETECTED:
        ESP_LOGI(TAG, "BMS: Input source %s — IINLIM %umA",
                 event->input.source == POWER_SOURCE_USB_HOST ? "USB host" : "adapter",
                 event->input.iinlim_ma);
        break;
    case POWER_EVENT_OTG_FAULT:
        ESP_LOGE(TAG, "BMS: OTG/Boost fault");
        break;
    case POWER_EVENT_SNAPSHOT_UPDATED:
        push_bas_to_ble();
        break;
    default:
        break;
    }
}

static void radar_task(void *arg)
{
    (void)arg;
    static const char *RTAG = "Radar";

    ESP_LOGI(RTAG, "Acconeer SDK v%s", acc_version_get());

    const acc_hal_a121_t *hal = acc_hal_rss_integration_get_implementation();
    if (!acc_rss_hal_register(hal))
    {
        ESP_LOGE(RTAG, "RSS HAL register failed");
        vTaskDelete(NULL);
        return;
    }

    /*
     * Active preset. Set for demo where we point the radar at humans from a distance, so we
     * keep VITALS_PRESET_SITTING here. For pet-harness deployment,
     * switch this to VITALS_PRESET_PET_HARNESS (close-range, wider HR search).
     */
    radar_system_t *system = radar_system_create(VITALS_PRESET_SITTING);
    if (system == NULL)
    {
        ESP_LOGE(RTAG, "Radar system creation failed");
        vTaskDelete(NULL);
        return;
    }

    if (!a121_vitals_prepare(system->handle, system->config, system->sensor,
                              &system->sensor_cal_result, system->buffer, system->buffer_size))
    {
        ESP_LOGE(RTAG, "Vitals prepare failed");
        radar_system_destroy(system);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(RTAG, "Radar initialized — monitoring vitals");

    a121_vitals_state_t prev_reported_state = A121_VITALS_STATE_INIT;

    /* Vitals publish state. last_*_ms == 0 means "no valid sample yet";
     * presence loss resets both so values from one subject never pair
     * with values from another across sessions. */
    int64_t last_hr_ms       = 0;
    int64_t last_br_ms       = 0;
    int64_t last_publish_ms  = 0;
    uint8_t cached_hr        = 0;
    uint8_t cached_hr_conf   = 0;
    uint8_t cached_br        = 0;

    while (true)
    {
        if (!acc_sensor_measure(system->sensor))
        {
            ESP_LOGE(RTAG, "acc_sensor_measure failed");
            break;
        }

        if (!acc_hal_integration_wait_for_sensor_interrupt(system->sensor_id, 3000))
        {
            ESP_LOGE(RTAG, "Sensor interrupt timeout");
            break;
        }

        if (!acc_sensor_read(system->sensor, system->buffer, system->buffer_size))
        {
            ESP_LOGE(RTAG, "acc_sensor_read failed");
            break;
        }

        a121_vitals_result_t result = {0};
        if (!a121_vitals_process(system->handle, system->buffer, &result))
        {
            ESP_LOGE(RTAG, "Vitals process failed");
            break;
        }

        /* Log state transitions */
        if (result.app_state != prev_reported_state)
        {
            static const char *state_names[] = {
                "INIT", "NO_PRESENCE", "INTRA_PRESENCE",
                "DETERMINE_DISTANCE", "ESTIMATE_BREATHING_RATE"
            };
            ESP_LOGI(RTAG, "State: %s", state_names[result.app_state]);
            prev_reported_state = result.app_state;
        }

        /* Forward to LED manager every iteration — it internally dedupes
         * and gates by verbosity, so this is cheap. Passing the current
         * result_ready + HR confidence lets it promote to VITALS_LOCKED. */
        led_manager_on_radar_state(result.app_state,
                                   result.result_ready,
                                   result.heart_rate_confidence);

        /* Report breathing rate */
        if (result.result_ready)
        {
            ESP_LOGI(RTAG, "Breathing: %d bpm", (int)result.breathing_rate);
        }

        /* Report heart rate when confidence is sufficient */
        if (result.heart_rate_confidence > RADAR_HR_CONFIDENCE_THRESHOLD)
        {
            ESP_LOGI(RTAG, "Heart rate: %d bpm (conf: %d%%)",
                     (int)result.heart_rate, (int)result.heart_rate_confidence);
        }

        /* ---- Publish vitals to BLE when both HR and BR are fresh. ---- */
        {
            const int64_t now_ms = esp_timer_get_time() / 1000;

            if (result.app_state == A121_VITALS_STATE_NO_PRESENCE) {
                last_hr_ms = 0;
                last_br_ms = 0;
            }

            bool hr_updated = false;
            bool br_updated = false;

            if (result.heart_rate_confidence > RADAR_HR_CONFIDENCE_THRESHOLD) {
                float hr = result.heart_rate;
                float conf = result.heart_rate_confidence;
                if (hr < 0.0f) hr = 0.0f; else if (hr > 255.0f) hr = 255.0f;
                if (conf > 100.0f) conf = 100.0f;
                cached_hr      = (uint8_t)hr;
                cached_hr_conf = (uint8_t)conf;
                last_hr_ms     = now_ms;
                hr_updated     = true;
            }

            if (result.result_ready) {
                float br = result.breathing_rate;
                if (br < 0.0f) br = 0.0f; else if (br > 255.0f) br = 255.0f;
                cached_br  = (uint8_t)br;
                last_br_ms = now_ms;
                br_updated = true;
            }

            const bool both_fresh =
                last_hr_ms != 0 && last_br_ms != 0 &&
                (now_ms - last_hr_ms) <= RADAR_VITALS_FRESHNESS_MS &&
                (now_ms - last_br_ms) <= RADAR_VITALS_FRESHNESS_MS;

            const bool rate_ok =
                (now_ms - last_publish_ms) >= RADAR_VITALS_MIN_PUBLISH_GAP_MS;

            if ((hr_updated || br_updated) && both_fresh && rate_ok &&
                ble_driver_is_authenticated() && rtc_is_time_set()) {

                ble_driver_set_hr(cached_hr, cached_hr_conf);
                ble_driver_set_br(cached_br);
                ble_driver_notify_vitals(true);
                last_publish_ms = now_ms;

                ESP_LOGI(RTAG, "Vitals notify: HR=%u bpm (conf %u%%), BR=%u bpm",
                         cached_hr, cached_hr_conf, cached_br);
            }
        }

        /* Handle error conditions */
        if (result.presence_result.processing_result.data_saturated)
        {
            ESP_LOGW(RTAG, "Data saturated — result unreliable");
        }

        if (result.presence_result.processing_result.frame_delayed)
        {
            ESP_LOGW(RTAG, "Frame delayed — reduce frame rate or read faster");
        }

        if (result.presence_result.processing_result.calibration_needed)
        {
            ESP_LOGI(RTAG, "Recalibrating sensor...");

            if (!radar_system_calibrate(system))
            {
                ESP_LOGE(RTAG, "Recalibration failed");
                break;
            }

            ESP_LOGI(RTAG, "Recalibration complete");

            if (!a121_vitals_prepare(system->handle, system->config, system->sensor,
                                      &system->sensor_cal_result, system->buffer, system->buffer_size))
            {
                ESP_LOGE(RTAG, "Re-prepare after calibration failed");
                break;
            }
        }

        /* Yield to other tasks — prevents watchdog trigger */
        vTaskDelay(1);
    }

    radar_system_destroy(system);
    ESP_LOGW(RTAG, "Radar task ended");
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Initialize Board Support Package */
    ESP_ERROR_CHECK(bsp_gpio_init());
    ESP_ERROR_CHECK(bsp_bus_init());

    /* Initialize status LED (single WS2812B). Must be up before the button
     * driver so gesture feedback lands on a live LED. The LED manager then
     * takes ownership — no code outside of the button helpers and the
     * manager itself should touch ws2812b_* directly after this point. */
    esp_err_t led_err = ws2812b_init(BTN_LED_COUNT);
    if (led_err != ESP_OK) {
        ESP_LOGE(TAG, "WS2812B init failed: %s", esp_err_to_name(led_err));
    } else {
        s_led_ready = true;
        ws2812b_fill_color(WS2812B_COLOR_OFF);
        ws2812b_refresh();

        esp_err_t mgr_err = led_manager_init();
        if (mgr_err != ESP_OK) {
            ESP_LOGE(TAG, "LED manager init failed: %s", esp_err_to_name(mgr_err));
        }
    }

    /* Initialize Power Management (BQ25896 + MAX17260). The BMS monitor
     * task is started AFTER ble_driver_init() below so its first
     * SNAPSHOT_UPDATED event doesn't land before the BLE battery caches
     * have been initialized (the sync handler writes into them). */
    esp_err_t pwr_err = power_manager_init();
    if (pwr_err != ESP_OK) {
        ESP_LOGE(TAG, "Power manager init failed: %s", esp_err_to_name(pwr_err));
        led_manager_boot_stage_fail("power");
    } else {
        led_manager_boot_stage_ok("power");
    }

    /* Initialize NFC hardware */
    nfc_init();
    led_manager_boot_stage_ok("nfc");

    /* Initialize BLE stack (NimBLE) */
    ble_config_t ble_cfg = BLE_CONFIG_DEFAULT();
    ble_cfg.device_name = "PetPulse";
    ble_cfg.appearance = BLE_APPEARANCE_HEART_RATE;
    ble_cfg.enable_bonding = true;
    ble_cfg.event_cb = ble_event_handler;
    esp_err_t ble_err = ble_driver_init(&ble_cfg);
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ble_err));
        led_manager_boot_stage_fail("ble");
    } else {
        ESP_LOGI(TAG, "BLE initialized — advertising as \"%s\"", ble_driver_get_device_name());
        led_manager_boot_stage_ok("ble");

        /* Now that the BLE battery caches are initialized, it's safe to
         * start the BMS monitor (which fires SNAPSHOT_UPDATED immediately). */
        if (pwr_err == ESP_OK) {
            power_manager_monitor_start(power_event_handler, NULL);
        }

        /* Initialize Apple Find My beacon. Only creates a FreeRTOS timer
         * and loads the key from NVS — no NimBLE calls happen here, so
         * it's safe to call before ble_on_sync() completes. The first
         * advertising burst fires FINDMY_DEFAULT_INTERVAL_S seconds later,
         * well after sync. */
        esp_err_t fm_err = findmy_init();
        if (fm_err == ESP_OK) {
            ESP_LOGI(TAG, "FindMy beacon initialized");
        } else if (fm_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "FindMy: no key data, beacon disabled");
        } else {
            ESP_LOGE(TAG, "FindMy init failed: %s", esp_err_to_name(fm_err));
        }
    }

    /* Start NFC tap-to-pair service (runs as background task) */
    nfc_pairing_config_t pair_cfg = NFC_PAIRING_CONFIG_DEFAULT();
    esp_err_t pair_err = nfc_pairing_start(&pair_cfg);
    if (pair_err != ESP_OK) {
        ESP_LOGE(TAG, "NFC pairing start failed: %s", esp_err_to_name(pair_err));
    } else {
        ESP_LOGI(TAG, "NFC tap-to-pair active");
    }

    /* Start IMU (BNO08x): data pump + significant-motion wake path.
       imu_data_task internally gates on BLE auth + RTC time-set before
       collecting; imu_motion_task waits on IMU init before arming. */
    imu_events_init();

    BaseType_t imu_data_ret = xTaskCreatePinnedToCore(
        imu_data_task, "imu_data", IMU_DATA_TASK_STACK, NULL,
        IMU_DATA_TASK_PRIORITY, NULL, IMU_TASK_CORE);
    if (imu_data_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create imu_data task");
        led_manager_boot_stage_fail("imu_data");
    } else {
        ESP_LOGI(TAG, "IMU data task started");
    }

    BaseType_t imu_motion_ret = xTaskCreatePinnedToCore(
        imu_motion_task, "imu_motion", IMU_MOTION_TASK_STACK, NULL,
        IMU_MOTION_TASK_PRIORITY, NULL, IMU_TASK_CORE);
    if (imu_motion_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create imu_motion task");
        led_manager_boot_stage_fail("imu_motion");
    } else {
        ESP_LOGI(TAG, "IMU motion task started");
    }

    if (imu_data_ret == pdPASS && imu_motion_ret == pdPASS) {
        led_manager_boot_stage_ok("imu");
    }

    /* Start environmental monitoring (SHT4x temp + humidity).
       Publishes to BLE env characteristic once auth + RTC are ready. */
    env_driver_config_t env_cfg = ENV_DRIVER_CONFIG_DEFAULT();
    env_cfg.sample_period_s = ENV_SAMPLE_PERIOD_S;
    esp_err_t env_err = env_driver_start(&env_cfg);
    if (env_err != ESP_OK) {
        ESP_LOGE(TAG, "Env driver start failed: %s", esp_err_to_name(env_err));
    } else {
        ESP_LOGI(TAG, "Env driver started (period=%us)",
                 (unsigned)env_cfg.sample_period_s);
    }

    /* Initialize button driver. Placed after BLE + power manager so the
     * event handler can safely query their state on the very first press. */
    button_config_t btn_cfg = BUTTON_CONFIG_DEFAULT();
    btn_cfg.event_cb = button_event_handler;
    esp_err_t btn_err = button_driver_init(&btn_cfg);
    if (btn_err != ESP_OK) {
        ESP_LOGE(TAG, "Button driver init failed: %s", esp_err_to_name(btn_err));
    } else {
        ESP_LOGI(TAG, "Button driver initialized");
    }

    /* Start radar processing task */
    BaseType_t ret = xTaskCreate(radar_task, "radar_task", RADAR_TASK_STACK_SIZE,
                                  NULL, RADAR_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radar task");
        led_manager_boot_stage_fail("radar");
    } else {
        ESP_LOGI(TAG, "Radar vitals task started");
        led_manager_boot_stage_ok("radar");
    }

    /* All init complete — transition LED out of BOOT state. Any stage that
     * called boot_stage_fail has latched LED_STATE_BOOT_FAIL, which will
     * keep showing instead of the SYSTEM_READY chirp. */
    led_manager_boot_complete();

    /* app_main returns — FreeRTOS scheduler continues running tasks */
}
