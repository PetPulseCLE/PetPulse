/**
 * @file env_driver.c
 * @brief Implementation of the environmental monitoring task.
 *
 * Architecture:
 *   - A single FreeRTOS task owns the SHT4x.  All I2C reads happen on that
 *     task, so no external locking is required around the sensor driver.
 *   - The cached reading is guarded by a mutex — callers from any context
 *     may invoke env_driver_get_reading() safely.
 *   - Publishing to BLE is gated on ble_driver_is_authenticated() +
 *     rtc_is_time_set() so that the env characteristic's timestamp is
 *     meaningful and only bonded centrals receive data.
 *   - Recovery: after N consecutive measurement failures the driver issues
 *     a soft-reset and re-reads the serial number.  If that succeeds the
 *     loop resumes; otherwise it keeps trying on the next period.
 */

#include "env_driver.h"

#include "bsp.h"
#include "ble_driver.h"
#include "rtc_driver.h"
#include "sht4x.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ========================================================================== */
/*  Private State                                                             */
/* ========================================================================== */

static const char *TAG = "env_driver";

typedef struct {
    /* Configuration — snapshot taken at start, immutable while running. */
    env_driver_config_t cfg;

    /* Task control. */
    TaskHandle_t      task_handle;
    SemaphoreHandle_t cache_mutex;
    volatile bool     running;

    /* Latest reading cache (protected by cache_mutex). */
    bool  reading_valid;
    float temperature_c;
    float humidity_pct;
} env_driver_state_t;

static env_driver_state_t s_env = {
    .task_handle   = NULL,
    .cache_mutex   = NULL,
    .running       = false,
    .reading_valid = false,
};

/* ========================================================================== */
/*  Helpers                                                                   */
/* ========================================================================== */

static uint32_t clamp_period_s(uint32_t period_s)
{
    if (period_s < ENV_MIN_PERIOD_S) return ENV_MIN_PERIOD_S;
    if (period_s > ENV_MAX_PERIOD_S) return ENV_MAX_PERIOD_S;
    return period_s;
}

/** Interruptible sleep — wakes early if env_driver_stop() was called. */
static void env_sleep_ms(uint32_t total_ms)
{
    const uint32_t slice_ms = 250;
    uint32_t remaining = total_ms;
    while (remaining > 0 && s_env.running) {
        uint32_t this_slice = remaining > slice_ms ? slice_ms : remaining;
        vTaskDelay(pdMS_TO_TICKS(this_slice));
        remaining -= this_slice;
    }
}

/** Wait for BLE auth + RTC time-set.  Returns false if stop was requested. */
static bool env_wait_for_ble_ready(void)
{
    ESP_LOGI(TAG, "Waiting for BLE auth + RTC time sync...");
    while (s_env.running) {
        if (ble_driver_is_authenticated() && rtc_is_time_set()) {
            ESP_LOGI(TAG, "BLE ready — starting env publish");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

/** Copy the latest reading into the cache under the mutex. */
static void env_cache_store(float temp_c, float rh_pct)
{
    if (s_env.cache_mutex == NULL) return;
    if (xSemaphoreTake(s_env.cache_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Cache mutex busy — reading not cached");
        return;
    }
    s_env.temperature_c = temp_c;
    s_env.humidity_pct  = rh_pct;
    s_env.reading_valid = true;
    xSemaphoreGive(s_env.cache_mutex);
}

/** Push the reading to the BLE env characteristic. */
static void env_publish_ble(float temp_c, float rh_pct, bool notify)
{
    ble_driver_set_temp(temp_c);
    ble_driver_set_humidity(rh_pct);
    /* notify_env auto-stamps with UTC time and dispatches GATT notify. */
    ble_driver_notify_env(notify);
}

/**
 * @brief Attempt to recover from persistent measurement failures.
 *
 * Issues a soft-reset and re-runs sht4x_init() which re-verifies the serial.
 * @return true on successful recovery.
 */
static bool env_attempt_recovery(i2c_master_bus_handle_t bus)
{
    ESP_LOGW(TAG, "Attempting SHT4x recovery (soft reset + re-init)");

    esp_err_t err = sht4x_soft_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed: %s", esp_err_to_name(err));
        return false;
    }

    /* SHT4x datasheet: 1 ms reset time; use 10 ms for margin. */
    vTaskDelay(pdMS_TO_TICKS(10));

    sht4x_data_t probe = {0};
    err = sht4x_init(bus, &probe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Re-init failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Recovery OK (serial 0x%08lX)", (unsigned long)probe.serial_number);
    return true;
}

/* ========================================================================== */
/*  Task Body                                                                 */
/* ========================================================================== */

static void env_task(void *arg)
{
    (void)arg;

    i2c_master_bus_handle_t bus = bsp_get_i2c_bus_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus handle NULL — aborting task");
        s_env.running     = false;
        s_env.task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* First-time sensor bring-up.  If this fails we retry periodically
       rather than giving up — the sensor might be cold or the bus may
       transiently glitch at boot. */
    sht4x_data_t data = {0};
    esp_err_t err = sht4x_init(bus, &data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial SHT4x init failed (%s) — will retry in loop",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SHT4x online (serial 0x%08lX)",
                 (unsigned long)data.serial_number);
    }

    /* Gate on BLE auth so publishes land on a paired central with valid
       timestamp.  If the gate is disabled (dev/bench), skip the wait. */
    if (s_env.cfg.gate_on_ble_auth) {
        if (!env_wait_for_ble_ready()) {
            ESP_LOGI(TAG, "Stop requested before BLE ready — exiting");
            goto task_exit;
        }
    }

    const uint32_t period_ms = clamp_period_s(s_env.cfg.sample_period_s) * 1000U;
    uint32_t consecutive_failures = 0;
    bool     sensor_online        = (err == ESP_OK);

    ESP_LOGI(TAG, "Env loop started — period %lu ms, precision 0x%02X",
             (unsigned long)period_ms, (unsigned)s_env.cfg.precision);

    while (s_env.running) {
        /* If the sensor never came up — or recovery was requested — try now. */
        if (!sensor_online) {
            if (env_attempt_recovery(bus)) {
                sensor_online        = true;
                consecutive_failures = 0;
            } else {
                env_sleep_ms(period_ms);
                continue;
            }
        }

        sht4x_data_t reading = {0};
        err = sht4x_measure(s_env.cfg.precision, &reading);
        if (err == ESP_OK) {
            consecutive_failures = 0;

            env_cache_store(reading.temperature, reading.humidity);
            env_publish_ble(reading.temperature, reading.humidity,
                            s_env.cfg.notify_on_publish);

            ESP_LOGD(TAG, "T=%.2f C  RH=%.1f %%",
                     reading.temperature, reading.humidity);
        } else {
            consecutive_failures++;
            ESP_LOGW(TAG, "Measure failed (%s) — %lu in a row",
                     esp_err_to_name(err),
                     (unsigned long)consecutive_failures);

            if (consecutive_failures >= ENV_MAX_FAIL_BEFORE_RECOVERY) {
                sensor_online        = false;
                consecutive_failures = 0;
                /* Next loop iteration will attempt recovery. */
            }
        }

        env_sleep_ms(period_ms);
    }

task_exit:
    ESP_LOGI(TAG, "Env task exiting");
    s_env.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ========================================================================== */
/*  Public API                                                                */
/* ========================================================================== */

esp_err_t env_driver_start(const env_driver_config_t *config)
{
    if (s_env.task_handle != NULL || s_env.running) {
        ESP_LOGW(TAG, "Already started");
        return ESP_ERR_INVALID_STATE;
    }

    if (bsp_get_i2c_bus_handle() == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialised — call bsp_bus_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* Snapshot configuration (or defaults). */
    if (config != NULL) {
        s_env.cfg = *config;
    } else {
        env_driver_config_t defaults = ENV_DRIVER_CONFIG_DEFAULT();
        s_env.cfg = defaults;
    }
    s_env.cfg.sample_period_s = clamp_period_s(s_env.cfg.sample_period_s);

    /* Allocate the cache mutex the first time through; reuse across
       start/stop cycles to avoid heap churn. */
    if (s_env.cache_mutex == NULL) {
        s_env.cache_mutex = xSemaphoreCreateMutex();
        if (s_env.cache_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to allocate cache mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_env.reading_valid = false;
    s_env.running       = true;

    BaseType_t ret = xTaskCreate(env_task, "env_task",
                                 ENV_TASK_STACK, NULL,
                                 ENV_TASK_PRIORITY, &s_env.task_handle);
    if (ret != pdPASS) {
        s_env.running     = false;
        s_env.task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create env task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Env driver started (period=%lus, gate=%d)",
             (unsigned long)s_env.cfg.sample_period_s,
             (int)s_env.cfg.gate_on_ble_auth);
    return ESP_OK;
}

esp_err_t env_driver_stop(void)
{
    if (!s_env.running && s_env.task_handle == NULL) {
        return ESP_OK;
    }

    s_env.running = false;

    /* Wait (bounded) for the task to delete itself.  The task exits on
       the next sleep-slice boundary (≤250 ms). */
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    while (s_env.task_handle != NULL && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (s_env.task_handle != NULL) {
        ESP_LOGW(TAG, "Env task did not exit in time — forcing delete");
        vTaskDelete(s_env.task_handle);
        s_env.task_handle = NULL;
    }

    return ESP_OK;
}

bool env_driver_has_reading(void)
{
    if (s_env.cache_mutex == NULL) return false;

    bool valid = false;
    if (xSemaphoreTake(s_env.cache_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        valid = s_env.reading_valid;
        xSemaphoreGive(s_env.cache_mutex);
    }
    return valid;
}

esp_err_t env_driver_get_reading(float *temperature_c, float *humidity_pct)
{
    if (s_env.cache_mutex == NULL) return ESP_ERR_INVALID_STATE;

    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_env.cache_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_env.reading_valid) {
            if (temperature_c) *temperature_c = s_env.temperature_c;
            if (humidity_pct)  *humidity_pct  = s_env.humidity_pct;
            result = ESP_OK;
        }
        xSemaphoreGive(s_env.cache_mutex);
    }
    return result;
}
