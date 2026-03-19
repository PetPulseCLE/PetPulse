#include "bms_driver.h"
#include "bq25896.h"
#include "max17260.h"
#include "max17260_reg.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TAG "PowerMgr"

// --- Battery Configuration (MAX17260) ---
const MAX17260Config gauge_config = {
    .design_cap = BATTERY_DESIGN_CAP_MAH,
    .v_empty = BATTERY_V_EMPTY_LSB,
    .i_chg_term = BATTERY_I_CHG_TERM_MA,
    .rsense = BATTERY_RSENSE_OHMS
};

// --- Internal State ---
typedef struct {
    volatile uint8_t insomnia;
    volatile uint8_t suppress_charge;
    bool gauge_ok;
    bool charger_ok;
    SemaphoreHandle_t i2c_mutex;
} PowerManagerState;

static PowerManagerState pm_state = {
    .insomnia = 0,
    .suppress_charge = 0,
    .gauge_ok = false,
    .charger_ok = false,
    .i2c_mutex = NULL
};

// Spinlock for thread-safe insomnia/suppress_charge counter access (ISR-safe)
static portMUX_TYPE pm_spinlock = portMUX_INITIALIZER_UNLOCKED;

// --- Helper Macros ---
#define I2C_TAKE_OR_RET(ret_val) \
    if (pm_state.i2c_mutex == NULL || xSemaphoreTake(pm_state.i2c_mutex, pdMS_TO_TICKS(BMS_MUTEX_TIMEOUT_MS)) != pdTRUE) { \
        ESP_LOGE(TAG, "I2C Mutex Timeout or Uninitialized"); \
        return ret_val; \
    }

#define I2C_GIVE() xSemaphoreGive(pm_state.i2c_mutex)

// --- Initialization ---

esp_err_t power_manager_init(void) {
    // I2C bus is initialized by BSP via bsp_bus_init() — no driver-level init needed.

    pm_state.i2c_mutex = xSemaphoreCreateMutex();
    

    // 2. Initialize Gauge (MAX17260) with retry logic
    I2C_TAKE_OR_RET(ESP_FAIL);
    size_t retry = BMS_INIT_RETRY_COUNT;
    while(retry > 0) {
        if (max17260_init(&gauge_config) == ESP_OK) {
            pm_state.gauge_ok = true;
            break;
        } else {
            // Gauge needs time or bus might be busy
            vTaskDelay(pdMS_TO_TICKS(BMS_INIT_RETRY_DELAY_MS));
        }
        retry--;
    }

    // 3. Initialize Charger (BQ25896) with retry logic
    retry = BMS_INIT_RETRY_COUNT;
    while(retry > 0) {
        if (bq25896_init() == ESP_OK) {
            pm_state.charger_ok = true;            
            // Configure Charge Parameters
            bq25896_set_charge_current(CHARGER_MAX_CURRENT_MA); 
            bq25896_set_vreg_voltage(CHARGER_MAX_VOLTAGE_MV);
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(BMS_INIT_RETRY_DELAY_MS));
        }
        retry--;
    }
    I2C_GIVE();

    if (pm_state.gauge_ok && pm_state.charger_ok) {
        ESP_LOGI(TAG, "Power Manager Init OK");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Power Manager Init Partial/Failed (Gauge:%d Charger:%d)", pm_state.gauge_ok, pm_state.charger_ok);
        return ESP_FAIL;
    }
}

// ============================================================================
// Event-Driven Monitor — ISR, poll helpers, task, public API
// ============================================================================

static TaskHandle_t monitor_task_handle = NULL;
static volatile bool monitor_running = false;
static power_event_cb_t monitor_cb = NULL;
static void *monitor_ctx = NULL;

static power_snapshot_t cached_snap;
static bool snap_valid = false;
static portMUX_TYPE snap_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Previous charger state for edge detection (0xFF = uninitialised sentinel)
static uint8_t prev_chrg_stat = 0xFF;
static uint8_t prev_vbus_stat = 0xFF;
static bool prev_low_bat  = false;
static bool prev_crit_bat = false;

// BQ25896 /INT falling-edge ISR
static void IRAM_ATTR bq25896_int_isr(void *arg) {
    (void)arg;
    BaseType_t woken = pdFALSE;
    if (monitor_task_handle != NULL) {
        vTaskNotifyGiveFromISR(monitor_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static inline void fire_event(const power_event_t *evt) {
    if (monitor_cb != NULL) {
        monitor_cb(evt, monitor_ctx);
    }
}

// Poll BQ25896 status + fault registers (task context)
static void monitor_poll_charger(void) {
    if (!pm_state.charger_ok) return;

    if (pm_state.i2c_mutex == NULL ||
        xSemaphoreTake(pm_state.i2c_mutex, pdMS_TO_TICKS(BMS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }

    REG0B status;
    bq25896_read_status(&status);

    REG0C faults;
    bq25896_read_faults(&faults);

    I2C_GIVE();

    // VBUS transitions
    bool now_usb = (status.VBUS_STAT != VBusStatNo && status.VBUS_STAT != VBusStatOTG);
    bool was_usb = (prev_vbus_stat != 0xFF &&
                    prev_vbus_stat != VBusStatNo && prev_vbus_stat != VBusStatOTG);

    if (now_usb && !was_usb) {
        power_event_t e = { .type = POWER_EVENT_USB_CONNECTED,
                            .status = { .chrg_stat = status.CHRG_STAT,
                                        .vbus_stat = status.VBUS_STAT } };
        fire_event(&e);
    } else if (!now_usb && was_usb) {
        power_event_t e = { .type = POWER_EVENT_USB_DISCONNECTED,
                            .status = { .chrg_stat = status.CHRG_STAT,
                                        .vbus_stat = status.VBUS_STAT } };
        fire_event(&e);
    }

    // Charge-state transitions
    if (status.CHRG_STAT != prev_chrg_stat && prev_chrg_stat != 0xFF) {
        if ((status.CHRG_STAT == ChrgStatPre || status.CHRG_STAT == ChrgStatFast) &&
            (prev_chrg_stat == ChrgStatNo || prev_chrg_stat == ChrgStatDone)) {
            power_event_t e = { .type = POWER_EVENT_CHARGE_START,
                                .status = { .chrg_stat = status.CHRG_STAT,
                                            .vbus_stat = status.VBUS_STAT } };
            fire_event(&e);
        } else if (status.CHRG_STAT == ChrgStatDone) {
            power_event_t e = { .type = POWER_EVENT_CHARGE_DONE,
                                .status = { .chrg_stat = status.CHRG_STAT,
                                            .vbus_stat = status.VBUS_STAT } };
            fire_event(&e);
        } else if (status.CHRG_STAT == ChrgStatNo &&
                   (prev_chrg_stat == ChrgStatPre || prev_chrg_stat == ChrgStatFast)) {
            power_event_t e = { .type = POWER_EVENT_CHARGE_STOPPED,
                                .status = { .chrg_stat = status.CHRG_STAT,
                                            .vbus_stat = status.VBUS_STAT } };
            fire_event(&e);
        }
    }

    // Fault detection (REG0C is read-to-clear for latched bits)
    if (faults.CHRG_FAULT != ChrgFaultNO || faults.NTC_FAULT != NtcFaultNo ||
        faults.BAT_FAULT || faults.WATCHDOG_FAULT) {
        power_event_t e = { .type = POWER_EVENT_CHARGER_FAULT,
                            .fault = { .chrg_fault     = faults.CHRG_FAULT,
                                       .ntc_fault      = faults.NTC_FAULT,
                                       .bat_fault      = faults.BAT_FAULT,
                                       .boost_fault    = faults.BOOST_FAULT,
                                       .watchdog_fault = faults.WATCHDOG_FAULT } };
        fire_event(&e);
    }
    if (faults.BOOST_FAULT) {
        power_event_t e = { .type = POWER_EVENT_OTG_FAULT,
                            .fault = { .chrg_fault     = faults.CHRG_FAULT,
                                       .ntc_fault      = faults.NTC_FAULT,
                                       .bat_fault      = faults.BAT_FAULT,
                                       .boost_fault    = faults.BOOST_FAULT,
                                       .watchdog_fault = faults.WATCHDOG_FAULT } };
        fire_event(&e);
    }

    prev_chrg_stat = status.CHRG_STAT;
    prev_vbus_stat = status.VBUS_STAT;
}

// Poll MAX17260 gauge and publish snapshot (task context)
static void monitor_poll_gauge(void) {
    if (!pm_state.gauge_ok) return;

    if (pm_state.i2c_mutex == NULL ||
        xSemaphoreTake(pm_state.i2c_mutex, pdMS_TO_TICKS(BMS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }

    power_snapshot_t snap = {0};
    snap.soc_pct        = (uint8_t)max17260_get_soc();
    snap.voltage_mv     = max17260_get_voltage();
    snap.current_ua     = max17260_get_current();
    snap.avg_current_ua = max17260_get_avg_current();
    snap.temperature_c  = max17260_get_temperature();
    snap.tte_min        = max17260_get_tte();
    snap.ttf_min        = max17260_get_ttf();

    if (pm_state.charger_ok) {
        REG0B st;
        bq25896_read_status(&st);
        snap.usb_connected = (st.VBUS_STAT != VBusStatNo && st.VBUS_STAT != VBusStatOTG);
        snap.charge_status = st.CHRG_STAT;
        snap.otg_enabled   = bq25896_is_otg_enabled();
    }

    I2C_GIVE();

    snap.update_tick = xTaskGetTickCount();

    portENTER_CRITICAL(&snap_spinlock);
    cached_snap = snap;
    snap_valid  = true;
    portEXIT_CRITICAL(&snap_spinlock);

    // Battery threshold edge detection
    bool low_now  = (snap.soc_pct <= BMS_LOW_BATTERY_PCT);
    bool crit_now = (snap.soc_pct <= BMS_CRITICAL_BATTERY_PCT) ||
                    (snap.voltage_mv > 500 &&
                     snap.voltage_mv < (uint16_t)(BATTERY_SHUTDOWN_VOLTAGE_V * 1000.0f));

    if (low_now && !prev_low_bat) {
        power_event_t e = { .type = POWER_EVENT_LOW_BATTERY,
                            .battery = { .soc_pct = snap.soc_pct,
                                         .voltage_mv = snap.voltage_mv } };
        fire_event(&e);
    }
    if (crit_now && !prev_crit_bat) {
        power_event_t e = { .type = POWER_EVENT_CRITICAL_BATTERY,
                            .battery = { .soc_pct = snap.soc_pct,
                                         .voltage_mv = snap.voltage_mv } };
        fire_event(&e);
    }

    prev_low_bat  = low_now;
    prev_crit_bat = crit_now;

    power_event_t e = { .type = POWER_EVENT_SNAPSHOT_UPDATED,
                        .battery = { .soc_pct = snap.soc_pct,
                                     .voltage_mv = snap.voltage_mv } };
    fire_event(&e);
}

// Monitor FreeRTOS task
static void bms_monitor_task(void *arg) {
    (void)arg;
    const TickType_t poll_ticks = pdMS_TO_TICKS(BMS_GAUGE_POLL_INTERVAL_S * 1000);

    ESP_LOGI(TAG, "BMS monitor started (poll every %ds)", BMS_GAUGE_POLL_INTERVAL_S);

    // Establish charger baseline (silent — no events)
    if (pm_state.charger_ok && pm_state.i2c_mutex != NULL) {
        if (xSemaphoreTake(pm_state.i2c_mutex, pdMS_TO_TICKS(BMS_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            REG0B st; bq25896_read_status(&st);
            REG0C ft; bq25896_read_faults(&ft);
            prev_chrg_stat = st.CHRG_STAT;
            prev_vbus_stat = st.VBUS_STAT;
            I2C_GIVE();
        }
    }

    // Initial gauge snapshot (fires SNAPSHOT_UPDATED + threshold events if needed)
    monitor_poll_gauge();

    while (monitor_running) {
        uint32_t got = ulTaskNotifyTake(pdTRUE, poll_ticks);

        if (got > 0) {
            // BQ25896 INT fired — debounce, then read
            vTaskDelay(pdMS_TO_TICKS(50));
            ulTaskNotifyTake(pdTRUE, 0);  // drain extras during debounce
            monitor_poll_charger();
        }

        // Refresh gauge on every wake (ISR or timeout)
        monitor_poll_gauge();
    }

    ESP_LOGI(TAG, "BMS monitor task exiting");
    monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t power_manager_monitor_start(power_event_cb_t callback, void *user_data) {
    if (monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Monitor already running");
        return ESP_ERR_INVALID_STATE;
    }

    monitor_cb      = callback;
    monitor_ctx     = user_data;
    monitor_running = true;
    prev_chrg_stat  = 0xFF;
    prev_vbus_stat  = 0xFF;
    prev_low_bat    = false;
    prev_crit_bat   = false;
    snap_valid      = false;

    // Install BQ25896 /INT ISR (falling edge, open-drain with pull-up)
    gpio_set_intr_type(BSP_INT_BMS, GPIO_INTR_NEGEDGE);
    esp_err_t err = gpio_isr_handler_add(BSP_INT_BMS, bq25896_int_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BMS ISR: %s", esp_err_to_name(err));
        monitor_running = false;
        return err;
    }

    // Clear any pending interrupt by reading status + fault registers
    if (pm_state.charger_ok && pm_state.i2c_mutex != NULL) {
        if (xSemaphoreTake(pm_state.i2c_mutex, pdMS_TO_TICKS(BMS_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            REG0B st; bq25896_read_status(&st);
            REG0C ft; bq25896_read_faults(&ft);
            I2C_GIVE();
        }
    }

    BaseType_t ret = xTaskCreate(bms_monitor_task, "bms_mon",
                                 BMS_MONITOR_TASK_STACK, NULL,
                                 BMS_MONITOR_TASK_PRIORITY, &monitor_task_handle);
    if (ret != pdPASS) {
        gpio_isr_handler_remove(BSP_INT_BMS);
        gpio_set_intr_type(BSP_INT_BMS, GPIO_INTR_DISABLE);
        monitor_running = false;
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "BMS monitor active (ISR on GPIO%d)", BSP_INT_BMS);
    return ESP_OK;
}

void power_manager_monitor_stop(void) {
    if (monitor_task_handle == NULL) return;

    // Disable ISR first to prevent new notifications
    gpio_isr_handler_remove(BSP_INT_BMS);
    gpio_set_intr_type(BSP_INT_BMS, GPIO_INTR_DISABLE);

    // Signal task to exit
    monitor_running = false;
    xTaskNotifyGive(monitor_task_handle);

    // Wait for clean exit (up to 1 s)
    for (int i = 0; i < 20 && monitor_task_handle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    monitor_cb  = NULL;
    monitor_ctx = NULL;
    ESP_LOGI(TAG, "BMS monitor stopped");
}

bool power_manager_get_snapshot(power_snapshot_t *snap) {
    if (snap == NULL) return false;
    portENTER_CRITICAL(&snap_spinlock);
    bool valid = snap_valid;
    if (valid) {
        *snap = cached_snap;
    }
    portEXIT_CRITICAL(&snap_spinlock);
    return valid;
}

// --- Status Checks ---

bool power_manager_gauge_is_ok(void) {
    bool ret = true;
    I2C_TAKE_OR_RET(false);
    
    // Check if we can read voltage as a health check
    if(max17260_get_voltage() == 0) {
        // Double check status register directly if voltage is 0 (unlikely for working battery)
        uint8_t raw_buf[2];
        if (i2c_master_write_read_device(BSP_I2C_PORT, MAX17260_ADDRESS, (uint8_t[]){REG_STATUS}, 1, raw_buf, 2, pdMS_TO_TICKS(50)) != ESP_OK) {
            ret = false;
        }
    }
    ret &= pm_state.gauge_ok;
    I2C_GIVE();
    return ret;
}

bool power_manager_is_shutdown_requested(void) {
    // 1. Check if external power is present. If so, never request shutdown.
    if (power_manager_get_usb_voltage() > USB_PRESENT_VOLTAGE_V) {
        return false;
    }

    // 2. Check for critical battery voltage (Safety Hard Limit)
    float vbat = power_manager_get_battery_voltage(POWER_IC_FUEL_GAUGE);
    if (vbat > 0.5f && vbat < BATTERY_SHUTDOWN_VOLTAGE_V) {
        ESP_LOGW(TAG, "Shutdown Requested: Critical Voltage (%.2fV)", vbat);
        return true;
    }

    // 3. Check for 0% SOC (Gauge logic)
    if (power_manager_get_pct() == 0) {
        ESP_LOGW(TAG, "Shutdown Requested: Battery Empty (0%%)");
        return true;
    }

    return false;
}

// --- Insomnia / Sleep Management ---

uint16_t power_manager_insomnia_level(void) {
    return pm_state.insomnia;
}

void power_manager_insomnia_enter(void) {
    portENTER_CRITICAL(&pm_spinlock);
    if (pm_state.insomnia < UINT8_MAX) {
        pm_state.insomnia++;
    } else {
        portEXIT_CRITICAL(&pm_spinlock);
        ESP_LOGE(TAG, "Insomnia counter overflow!");
        return;
    }
    portEXIT_CRITICAL(&pm_spinlock);
}

void power_manager_insomnia_exit(void) {
    portENTER_CRITICAL(&pm_spinlock);
    if (pm_state.insomnia > 0) {
        pm_state.insomnia--;
    } else {
        portEXIT_CRITICAL(&pm_spinlock);
        ESP_LOGE(TAG, "Insomnia counter underflow!");
        return;
    }
    portEXIT_CRITICAL(&pm_spinlock);
}

bool power_manager_sleep_available(void) {
    return pm_state.insomnia == 0;
}

void power_manager_sleep(void) {
    if (power_manager_sleep_available()) {
        ESP_LOGI(TAG, "Entering Light Sleep...");

        // BQ25896 INT wakeup — active-low, open-drain
        gpio_wakeup_enable(BSP_INT_BMS, GPIO_INTR_LOW_LEVEL);
        // Button wakeup — active-low with pull-up
        gpio_wakeup_enable(BSP_BUTTON, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();

        esp_light_sleep_start();
        ESP_LOGI(TAG, "Woke up from Light Sleep");
    }
}

void power_manager_shutdown(void) {
    power_manager_insomnia_enter();
    power_manager_monitor_stop();

    ESP_LOGW(TAG, "System Shutdown Requested");

    // BQ25896 INT (GPIO6, RTC-capable) as deep-sleep wakeup.
    // USB plug-in pulls INT low, resuming the system.
    esp_sleep_enable_ext1_wakeup(1ULL << BSP_INT_BMS, ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
}

void power_manager_off(void) {
    power_manager_monitor_stop();

    vTaskDelay(pdMS_TO_TICKS(50));

    I2C_TAKE_OR_RET();
    bq25896_poweroff();
    I2C_GIVE();

    // Power cuts here if on battery. On USB we may stay alive.
    vTaskDelay(pdMS_TO_TICKS(100)); 
}

void power_manager_reset(void) {
    esp_restart();
}

// --- Battery Info ---

uint8_t power_manager_get_pct(void) {
    I2C_TAKE_OR_RET(0);
    uint8_t ret = (uint8_t)max17260_get_soc();
    I2C_GIVE();
    return ret;
}

uint8_t power_manager_get_bat_health_pct(void) {
    I2C_TAKE_OR_RET(0);
    uint8_t ret = max17260_get_age();
    I2C_GIVE();
    return ret;
}

// --- Charging Logic ---

bool power_manager_is_charging(void) {
    I2C_TAKE_OR_RET(false);
    bool ret = bq25896_is_charging();
    I2C_GIVE();
    return ret;
}

bool power_manager_is_charging_done(void) {
    I2C_TAKE_OR_RET(false);
    bool ret = bq25896_is_charging_done();
    I2C_GIVE();
    return ret;
}

// --- OTG (Boost Mode) ---

bool power_manager_enable_otg(void) {
    I2C_TAKE_OR_RET(false);
    // Increase boost limit momentarily to start
    bq25896_set_boost_lim(BoostLim_2150);
    bq25896_enable_otg();
    vTaskDelay(pdMS_TO_TICKS(30)); // Wait for soft start
    bool ret = bq25896_is_otg_enabled();
    // Restore boost limit
    bq25896_set_boost_lim(BoostLim_1400);
    I2C_GIVE();
    return ret;
}

void power_manager_disable_otg(void) {
    I2C_TAKE_OR_RET();
    bq25896_disable_otg();
    I2C_GIVE();
}

bool power_manager_is_otg_enabled(void) {
    I2C_TAKE_OR_RET(false);
    bool ret = bq25896_is_otg_enabled();
    I2C_GIVE();
    return ret;
}

bool power_manager_check_otg_fault(void) {
    I2C_TAKE_OR_RET(false);
    bool ret = bq25896_check_otg_fault();
    I2C_GIVE();
    return ret;
}

void power_manager_check_otg_status(void) {
    I2C_TAKE_OR_RET();
    if(bq25896_check_otg_fault()) {
        bq25896_disable_otg();
    }
    I2C_GIVE();
}

// --- Detailed Electrical Values ---

float power_manager_get_battery_charge_voltage_limit(void) {
    I2C_TAKE_OR_RET(0.0f);
    float ret = (float)bq25896_get_vreg_voltage() / 1000.0f;
    I2C_GIVE();
    return ret;
}

void power_manager_set_battery_charge_voltage_limit(float voltage) {
    I2C_TAKE_OR_RET();
    // Adding 0.0005 for rounding safety
    bq25896_set_vreg_voltage((uint16_t)(voltage * 1000.0f + 0.0005f));
    I2C_GIVE();
}

uint32_t power_manager_get_battery_remaining_capacity(void) {
    I2C_TAKE_OR_RET(0);
    uint32_t ret = max17260_get_capacity();
    I2C_GIVE();
    return ret;
}

uint32_t power_manager_get_battery_full_capacity(void) {
    I2C_TAKE_OR_RET(0);
    uint32_t ret = max17260_get_full_capacity();
    I2C_GIVE();
    return ret;
}

uint32_t power_manager_get_battery_design_capacity(void) {
    return gauge_config.design_cap;
}

float power_manager_get_battery_voltage(power_ic_type_t ic) {
    float ret = 0.0f;
    I2C_TAKE_OR_RET(0.0f);
    if(ic == POWER_IC_CHARGER) {
        ret = (float)bq25896_get_vbat_voltage() / 1000.0f;
    } else if(ic == POWER_IC_FUEL_GAUGE) {
        ret = (float)max17260_get_voltage() / 1000.0f;
    }
    I2C_GIVE();
    return ret;
}

float power_manager_get_battery_current(power_ic_type_t ic) {
    float ret = 0.0f;
    I2C_TAKE_OR_RET(0.0f);
    if(ic == POWER_IC_CHARGER) {
        ret = (float)bq25896_get_vbat_current() / 1000.0f;
    } else if(ic == POWER_IC_FUEL_GAUGE) {
        // max17260 returns uA, convert to Amps
        ret = (float)max17260_get_current() / 1000000.0f;
    }
    I2C_GIVE();
    return ret;
}

static float power_manager_get_battery_temperature_internal(power_ic_type_t ic) {
    float ret = 0.0f;
    if(ic == POWER_IC_CHARGER) {
        // Linear approximation logic from original file
        ret = (71.0f - (float)bq25896_get_ntc_mpct() / 1000.0f) / 0.6f;
    } else if(ic == POWER_IC_FUEL_GAUGE) {
        ret = max17260_get_temperature();
    }
    return ret;
}

float power_manager_get_battery_temperature(power_ic_type_t ic) {
    I2C_TAKE_OR_RET(0.0f);
    float ret = power_manager_get_battery_temperature_internal(ic);
    I2C_GIVE();
    return ret;
}

float power_manager_get_usb_voltage(void) {
    I2C_TAKE_OR_RET(0.0f);
    float ret = (float)bq25896_get_vbus_voltage() / 1000.0f;
    I2C_GIVE();
    return ret;
}

// --- Charge Suppression ---

void power_manager_suppress_charge_enter(void) {
    portENTER_CRITICAL(&pm_spinlock);
    bool disable_charging = (pm_state.suppress_charge == 0);
    pm_state.suppress_charge++;
    portEXIT_CRITICAL(&pm_spinlock);

    if (disable_charging) {
        I2C_TAKE_OR_RET();
        bq25896_disable_charging();
        I2C_GIVE();
    }
}

void power_manager_suppress_charge_exit(void) {
    portENTER_CRITICAL(&pm_spinlock);
    if (pm_state.suppress_charge == 0) {
        portEXIT_CRITICAL(&pm_spinlock);
        ESP_LOGE(TAG, "Suppress charge counter underflow!");
        return;
    }
    pm_state.suppress_charge--;
    bool enable_charging = (pm_state.suppress_charge == 0);
    portEXIT_CRITICAL(&pm_spinlock);

    if (enable_charging) {
        I2C_TAKE_OR_RET();
        bq25896_enable_charging();
        I2C_GIVE();
    }
}

// --- Info / Debug Callbacks ---

void power_manager_info_get(power_info_callback_t out, void* context) {
    if (!out) return;

    char buf[64];

    // Info Header
    out("power.info.major", "2", context);
    out("power.info.minor", "1", context);

    // Charge Level
    uint8_t charge = power_manager_get_pct();
    snprintf(buf, sizeof(buf), "%u", charge);
    out("charge.level", buf, context);

    // Charge State
    const char* charge_state = "discharging";
    if (power_manager_is_charging()) {
        if ((charge < 100) && (!power_manager_is_charging_done())) {
            charge_state = "charging";
        } else {
            charge_state = "charged";
        }
    }
    out("charge.state", charge_state, context);

    // Voltage Limit
    snprintf(buf, sizeof(buf), "%u", (uint16_t)(power_manager_get_battery_charge_voltage_limit() * 1000.f));
    out("charge.voltage.limit", buf, context);

    // Battery Voltage
    snprintf(buf, sizeof(buf), "%u", (uint16_t)(power_manager_get_battery_voltage(POWER_IC_FUEL_GAUGE) * 1000.f));
    out("battery.voltage", buf, context);

    // Battery Current
    snprintf(buf, sizeof(buf), "%d", (int16_t)(power_manager_get_battery_current(POWER_IC_FUEL_GAUGE) * 1000.f));
    out("battery.current", buf, context);

    // Temp
    snprintf(buf, sizeof(buf), "%d", (int16_t)power_manager_get_battery_temperature(POWER_IC_FUEL_GAUGE));
    out("battery.temp", buf, context);

    // Health
    snprintf(buf, sizeof(buf), "%u", power_manager_get_bat_health_pct());
    out("battery.health", buf, context);

    // Capacities
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)power_manager_get_battery_remaining_capacity());
    out("capacity.remain", buf, context);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)power_manager_get_battery_full_capacity());
    out("capacity.full", buf, context);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)power_manager_get_battery_design_capacity());
    out("capacity.design", buf, context);
}

void power_manager_debug_get(power_info_callback_t out, void* context) {
    if (!out) return;
    char buf[64];

    I2C_TAKE_OR_RET();

    // Debug Header
    out("format.major", "1", context);
    out("format.minor", "0", context);

    // --- Charger Info (BQ25896) ---
    snprintf(buf, sizeof(buf), "%d", bq25896_get_vbus_voltage());
    out("charger.vbus", buf, context);
    snprintf(buf, sizeof(buf), "%d", bq25896_get_vsys_voltage());
    out("charger.vsys", buf, context);
    snprintf(buf, sizeof(buf), "%d", bq25896_get_vbat_voltage());
    out("charger.vbat", buf, context);
    snprintf(buf, sizeof(buf), "%d", bq25896_get_vreg_voltage());
    out("charger.vreg", buf, context);
    snprintf(buf, sizeof(buf), "%d", bq25896_get_vbat_current());
    out("charger.current", buf, context);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)bq25896_get_ntc_mpct());
    out("charger.ntc", buf, context);

    // --- Gauge Info (MAX17260) ---
    snprintf(buf, sizeof(buf), "%d", max17260_get_soc());
    out("gauge.state.charge", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_age());
    out("gauge.state.health", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_voltage());
    out("gauge.voltage", buf, context);
    snprintf(buf, sizeof(buf), "%ld", (long)max17260_get_current());
    out("gauge.current", buf, context);
    snprintf(buf, sizeof(buf), "%ld", (long)max17260_get_avg_current());
    out("gauge.avg_current", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_capacity());
    out("gauge.capacity.remain", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_full_capacity());
    out("gauge.capacity.full", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_design_capacity());
    out("gauge.capacity.design", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_cycles());
    out("gauge.cycles", buf, context);
    snprintf(buf, sizeof(buf), "%.1f", max17260_get_temperature());
    out("gauge.temperature", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_tte());
    out("gauge.tte", buf, context);
    snprintf(buf, sizeof(buf), "%d", max17260_get_ttf());
    out("gauge.ttf", buf, context);

    I2C_GIVE();
}