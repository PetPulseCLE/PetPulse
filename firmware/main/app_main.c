#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h" // Board Support Package
#include "nfc_driver.h" // NFC Driver
#include "nfc_pairing.h" // NFC Tap-to-Pair
#include "ble_driver.h" // BLE Driver
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
#include "bms_driver.h"

static const char *TAG = "Main";

#define RADAR_TASK_STACK_SIZE  8192
#define RADAR_TASK_PRIORITY    5

/* BLE event callback — logs connection/pairing events */
static void ble_event_handler(const ble_event_t *event, void *user_data)
{
    (void)user_data;
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
}

/* Power management event callback — logs BMS events */
static void power_event_handler(const power_event_t *event, void *user_data)
{
    (void)user_data;
    switch (event->type) {
    case POWER_EVENT_USB_CONNECTED:
        ESP_LOGI(TAG, "BMS: USB connected");
        break;
    case POWER_EVENT_USB_DISCONNECTED:
        ESP_LOGI(TAG, "BMS: USB disconnected");
        break;
    case POWER_EVENT_CHARGE_START:
        ESP_LOGI(TAG, "BMS: Charging started");
        break;
    case POWER_EVENT_CHARGE_DONE:
        ESP_LOGI(TAG, "BMS: Charge complete");
        break;
    case POWER_EVENT_CHARGE_STOPPED:
        ESP_LOGW(TAG, "BMS: Charging stopped");
        break;
    case POWER_EVENT_LOW_BATTERY:
        ESP_LOGW(TAG, "BMS: Low battery (%d%%, %dmV)",
                 event->battery.soc_pct, event->battery.voltage_mv);
        break;
    case POWER_EVENT_CRITICAL_BATTERY:
        ESP_LOGE(TAG, "BMS: CRITICAL battery (%d%%, %dmV)",
                 event->battery.soc_pct, event->battery.voltage_mv);
        break;
    case POWER_EVENT_CHARGER_FAULT:
        ESP_LOGE(TAG, "BMS: Charger fault (chrg=%d ntc=%d bat=%d wd=%d)",
                 event->fault.chrg_fault, event->fault.ntc_fault,
                 event->fault.bat_fault, event->fault.watchdog_fault);
        break;
    case POWER_EVENT_OTG_FAULT:
        ESP_LOGE(TAG, "BMS: OTG/Boost fault");
        break;
    case POWER_EVENT_SNAPSHOT_UPDATED: {
        /* Push battery level to BLE if SOC changed */
        power_snapshot_t snap;
        if (power_manager_get_snapshot(&snap)) {
            ble_driver_notify_battery_level(snap.soc_pct);
        }
        break;
    }
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

        /* Report breathing rate */
        if (result.result_ready)
        {
            ESP_LOGI(RTAG, "Breathing: %d bpm", (int)result.breathing_rate);
        }

        /* Report heart rate when confidence is sufficient */
        if (result.heart_rate_confidence > 50.0f)
        {
            ESP_LOGI(RTAG, "Heart rate: %d bpm (conf: %d%%)",
                     (int)result.heart_rate, (int)result.heart_rate_confidence);
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

    /* Initialize Power Management (BQ25896 + MAX17260) */
    esp_err_t pwr_err = power_manager_init();
    if (pwr_err != ESP_OK) {
        ESP_LOGE(TAG, "Power manager init failed: %s", esp_err_to_name(pwr_err));
    } else {
        power_manager_monitor_start(power_event_handler, NULL);
    }

    /* Initialize NFC hardware */
    nfc_init();

    /* Initialize BLE stack (NimBLE) */
    ble_config_t ble_cfg = BLE_CONFIG_DEFAULT();
    ble_cfg.device_name = "PetPulse";
    ble_cfg.appearance = BLE_APPEARANCE_HEART_RATE;
    ble_cfg.enable_bonding = true;
    ble_cfg.event_cb = ble_event_handler;
    esp_err_t ble_err = ble_driver_init(&ble_cfg);
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ble_err));
    } else {
        ESP_LOGI(TAG, "BLE initialized — advertising as \"%s\"", ble_driver_get_device_name());
    }

    /* Start NFC tap-to-pair service (runs as background task) */
    nfc_pairing_config_t pair_cfg = NFC_PAIRING_CONFIG_DEFAULT();
    esp_err_t pair_err = nfc_pairing_start(&pair_cfg);
    if (pair_err != ESP_OK) {
        ESP_LOGE(TAG, "NFC pairing start failed: %s", esp_err_to_name(pair_err));
    } else {
        ESP_LOGI(TAG, "NFC tap-to-pair active");
    }

    /* Start radar processing task */
    BaseType_t ret = xTaskCreate(radar_task, "radar_task", RADAR_TASK_STACK_SIZE,
                                  NULL, RADAR_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radar task");
    } else {
        ESP_LOGI(TAG, "Radar vitals task started");
    }

    /* app_main returns — FreeRTOS scheduler continues running tasks */
}
