/**
 * @file led_manager.c
 * @brief System-wide LED feedback manager implementation.
 *
 * See led_manager.h for the design overview.
 */

#include "led_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"

static const char *TAG = "LED_MGR";

/* ========================================================================== */
/*  Internal event queue                                                      */
/* ========================================================================== */

typedef enum {
    LED_EV_SET_BASE,
    LED_EV_CLEAR_BASE,
    LED_EV_CLEAR_ALL_BASE,
    LED_EV_NOTIFY,
    LED_EV_CLEAR_OVERLAY,
    LED_EV_OVERRIDE_SET,
    LED_EV_OVERRIDE_CLEAR,
    LED_EV_SET_VERBOSITY,
    LED_EV_SET_BRIGHTNESS,
    LED_EV_SHUTDOWN,
} led_ev_kind_t;

typedef struct {
    led_ev_kind_t    kind;
    led_state_t      state;
    ws2812b_color_t  color;
    led_pattern_t    pattern;
    uint32_t         blink_count;
    uint32_t         duration_ms;
    uint8_t          u8;        /* verbosity / brightness payload */
} led_event_msg_t;

/* ========================================================================== */
/*  Priority table                                                            */
/* ========================================================================== */

/**
 * Maps a base state to its rendering parameters. The array index is the
 * led_state_t value, which also doubles as the priority (higher enum value ==
 * higher priority, see the header's declaration order).
 */
typedef struct {
    ws2812b_color_t    color;
    led_pattern_t      pattern;
    uint32_t           period_ms;
    led_verbosity_t    min_verbosity;
    bool               render;      /* false means "no visual" */
} led_state_def_t;

/* Note: some entries are overridden at render time (e.g. CHARGING period
 * scales with SOC). Values here are sensible defaults. */
static const led_state_def_t s_state_def[LED_STATE__COUNT] = {
    [LED_STATE_OFF] = {
        .color = WS2812B_COLOR_OFF, .pattern = LED_PAT_OFF,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_OFF, .render = true,
    },
    [LED_STATE_SYSTEM_READY] = {
        .color = WS2812B_COLOR_OFF, .pattern = LED_PAT_OFF,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_STATUS, .render = false,
    },
    [LED_STATE_BLE_CONNECTED_IDLE] = {
        .color = WS2812B_COLOR_OFF, .pattern = LED_PAT_OFF,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_STATUS, .render = false,
    },
    [LED_STATE_BLE_ADVERTISING] = {
        .color = WS2812B_COLOR_BLUE, .pattern = LED_PAT_BREATHE,
        .period_ms = 2400, .min_verbosity = LED_VERBOSITY_STATUS, .render = true,
    },
    [LED_STATE_RADAR_INTRA] = {
        .color = WS2812B_COLOR_YELLOW, .pattern = LED_PAT_BREATHE,
        .period_ms = 1600, .min_verbosity = LED_VERBOSITY_FULL, .render = true,
    },
    [LED_STATE_RADAR_DISTANCE] = {
        .color = WS2812B_COLOR_ORANGE, .pattern = LED_PAT_BREATHE,
        .period_ms = 1200, .min_verbosity = LED_VERBOSITY_FULL, .render = true,
    },
    [LED_STATE_RADAR_ESTIMATING] = {
        .color = WS2812B_COLOR_SPRING_GREEN, .pattern = LED_PAT_BREATHE,
        .period_ms = 900, .min_verbosity = LED_VERBOSITY_FULL, .render = true,
    },
    [LED_STATE_VITALS_LOCKED] = {
        .color = WS2812B_COLOR_GREEN, .pattern = LED_PAT_STEADY,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_FULL, .render = true,
    },
    [LED_STATE_BOOT] = {
        .color = WS2812B_COLOR_DIM_WHITE, .pattern = LED_PAT_STEADY,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_STATUS, .render = true,
    },
    [LED_STATE_USB_CONNECTED] = {
        .color = WS2812B_COLOR_BLUE, .pattern = LED_PAT_BREATHE,
        .period_ms = 3000, .min_verbosity = LED_VERBOSITY_POWER, .render = true,
    },
    [LED_STATE_CHARGE_DONE] = {
        .color = WS2812B_COLOR_GREEN, .pattern = LED_PAT_STEADY,
        .period_ms = 0, .min_verbosity = LED_VERBOSITY_POWER, .render = true,
    },
    [LED_STATE_CHARGING_TOPOFF] = {
        .color = WS2812B_COLOR_ORANGE, .pattern = LED_PAT_BREATHE,
        .period_ms = 1800, .min_verbosity = LED_VERBOSITY_POWER, .render = true,
    },
    [LED_STATE_CHARGING] = {
        .color = WS2812B_COLOR_RED, .pattern = LED_PAT_BREATHE,
        .period_ms = 2500, .min_verbosity = LED_VERBOSITY_POWER, .render = true,
    },
    [LED_STATE_LOW_BATTERY] = {
        .color = WS2812B_COLOR_ORANGE, .pattern = LED_PAT_BREATHE,
        .period_ms = 3000, .min_verbosity = LED_VERBOSITY_POWER, .render = true,
    },
    [LED_STATE_CRITICAL_BATTERY] = {
        .color = WS2812B_COLOR_RED, .pattern = LED_PAT_DOUBLE_BLINK_REPEAT,
        .period_ms = 150, .min_verbosity = LED_VERBOSITY_CRITICAL, .render = true,
    },
    [LED_STATE_CHARGER_FAULT] = {
        .color = WS2812B_COLOR_RED, .pattern = LED_PAT_STROBE,
        .period_ms = 100, .min_verbosity = LED_VERBOSITY_CRITICAL, .render = true,
    },
    [LED_STATE_OTG_FAULT] = {
        .color = WS2812B_COLOR_RED, .pattern = LED_PAT_STROBE,
        .period_ms = 120, .min_verbosity = LED_VERBOSITY_CRITICAL, .render = true,
    },
    [LED_STATE_BOOT_FAIL] = {
        .color = WS2812B_COLOR_RED, .pattern = LED_PAT_BLINK,
        .period_ms = 800, .min_verbosity = LED_VERBOSITY_CRITICAL, .render = true,
    },
};

/* ========================================================================== */
/*  Sine LUT for smooth breathe                                               */
/* ========================================================================== */

/**
 * 64-entry sine LUT mapped to [0, 255], one full cycle. Computed from a
 * half-rectified sine so the LED fades in, plateaus briefly at peak, and
 * fades back to off — which reads more natural than a symmetric wave.
 */
static const uint8_t s_sine_lut[64] = {
      0,   2,   5,  10,  17,  24,  34,  44,
     55,  68,  81,  94, 108, 122, 136, 150,
    164, 177, 190, 202, 213, 223, 232, 240,
    246, 251, 254, 255, 254, 251, 246, 240,
    232, 223, 213, 202, 190, 177, 164, 150,
    136, 122, 108,  94,  81,  68,  55,  44,
     34,  24,  17,  10,   5,   2,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
};

/* ========================================================================== */
/*  Manager state                                                             */
/* ========================================================================== */

typedef struct {
    bool             initialized;
    bool             task_running;
    QueueHandle_t    queue;
    TaskHandle_t     task;
    SemaphoreHandle_t state_lock;

    /* Base state: bitset of asserted states, resolved by priority (highest
     * enum value wins). */
    uint32_t         base_asserted_mask;  /* bit i set if state i asserted */
    led_state_t      base_active;         /* currently rendered base state */

    /* Overlay: transient one-shot */
    bool             overlay_active;
    ws2812b_color_t  overlay_color;
    led_pattern_t    overlay_pattern;
    uint32_t         overlay_blink_count;
    uint32_t         overlay_duration_ms;
    TickType_t       overlay_start_tick;

    /* Override: hard-set steady */
    bool             override_active;
    ws2812b_color_t  override_color;

    /* Config */
    led_verbosity_t  verbosity;
    uint8_t          max_brightness;

    /* Cached context for dynamic state tuning (charging period vs SOC) */
    uint8_t          last_soc_pct;

    /* Animation tick counter (monotonically increasing, in LED_MANAGER_TICK_MS) */
    uint32_t         tick_count;

    /* Last rendered pixel — avoids redundant RMT transmissions. */
    ws2812b_color_t  last_rendered;
    bool             last_rendered_valid;
} led_mgr_t;

static led_mgr_t s_mgr;

/* ========================================================================== */
/*  Forward declarations                                                      */
/* ========================================================================== */

static void led_task(void *arg);
static void render_tick(void);
static ws2812b_color_t compute_base_pixel(uint32_t tick);
static ws2812b_color_t compute_overlay_pixel(uint32_t tick, bool *done);
static ws2812b_color_t apply_brightness(ws2812b_color_t c, uint8_t scale);
static void apply_verbosity_gate(void);
static led_state_t resolve_base_from_mask(uint32_t mask);
static void post_event(const led_event_msg_t *msg);
static esp_err_t load_nvs(void);
static void save_nvs(void);

/* Short-form brightness scale helper. */
static inline uint8_t u8mul(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint32_t)a * (uint32_t)b) / 255U);
}

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

esp_err_t led_manager_init(void)
{
    if (s_mgr.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.verbosity      = LED_VERBOSITY_DEFAULT;
    s_mgr.max_brightness = LED_MANAGER_DEFAULT_BRIGHT;
    s_mgr.base_active    = LED_STATE_OFF;
    /* Always assert OFF as the floor, so the resolve never returns garbage. */
    s_mgr.base_asserted_mask = (1U << LED_STATE_OFF);

    (void)load_nvs();   /* Best-effort — defaults stand if read fails. */

    s_mgr.state_lock = xSemaphoreCreateMutex();
    if (!s_mgr.state_lock) {
        ESP_LOGE(TAG, "state_lock alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s_mgr.queue = xQueueCreate(LED_MANAGER_QUEUE_DEPTH, sizeof(led_event_msg_t));
    if (!s_mgr.queue) {
        vSemaphoreDelete(s_mgr.state_lock);
        s_mgr.state_lock = NULL;
        ESP_LOGE(TAG, "queue alloc failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(led_task, "led_mgr",
                                LED_MANAGER_TASK_STACK, NULL,
                                LED_MANAGER_TASK_PRIORITY, &s_mgr.task);
    if (ok != pdPASS) {
        vQueueDelete(s_mgr.queue);
        s_mgr.queue = NULL;
        vSemaphoreDelete(s_mgr.state_lock);
        s_mgr.state_lock = NULL;
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }

    s_mgr.task_running = true;
    s_mgr.initialized  = true;

    ESP_LOGI(TAG, "LED manager ready (verbosity=%d, brightness=%u)",
             (int)s_mgr.verbosity, (unsigned)s_mgr.max_brightness);
    return ESP_OK;
}

esp_err_t led_manager_deinit(void)
{
    if (!s_mgr.initialized) return ESP_ERR_INVALID_STATE;

    led_event_msg_t msg = { .kind = LED_EV_SHUTDOWN };
    post_event(&msg);

    /* Wait for the task to exit cleanly. */
    for (int i = 0; i < 50 && s_mgr.task_running; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (s_mgr.queue)      { vQueueDelete(s_mgr.queue);       s_mgr.queue      = NULL; }
    if (s_mgr.state_lock) { vSemaphoreDelete(s_mgr.state_lock); s_mgr.state_lock = NULL; }
    s_mgr.initialized = false;
    return ESP_OK;
}

bool led_manager_is_ready(void) { return s_mgr.initialized; }

/* ========================================================================== */
/*  Verbosity / brightness                                                    */
/* ========================================================================== */

void led_manager_set_verbosity(led_verbosity_t v)
{
    if (v > LED_VERBOSITY_DEBUG) v = LED_VERBOSITY_DEBUG;
    led_event_msg_t msg = { .kind = LED_EV_SET_VERBOSITY, .u8 = (uint8_t)v };
    post_event(&msg);
}

led_verbosity_t led_manager_get_verbosity(void)
{
    led_verbosity_t v;
    xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
    v = s_mgr.verbosity;
    xSemaphoreGive(s_mgr.state_lock);
    return v;
}

void led_manager_set_max_brightness(uint8_t brightness)
{
    led_event_msg_t msg = { .kind = LED_EV_SET_BRIGHTNESS, .u8 = brightness };
    post_event(&msg);
}

uint8_t led_manager_get_max_brightness(void)
{
    uint8_t b;
    xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
    b = s_mgr.max_brightness;
    xSemaphoreGive(s_mgr.state_lock);
    return b;
}

/* ========================================================================== */
/*  Base state                                                                */
/* ========================================================================== */

void led_manager_set_base(led_state_t state)
{
    if ((unsigned)state >= LED_STATE__COUNT) return;
    led_event_msg_t msg = { .kind = LED_EV_SET_BASE, .state = state };
    post_event(&msg);
}

void led_manager_clear_base(led_state_t state)
{
    if ((unsigned)state >= LED_STATE__COUNT) return;
    led_event_msg_t msg = { .kind = LED_EV_CLEAR_BASE, .state = state };
    post_event(&msg);
}

void led_manager_clear_all_base(void)
{
    led_event_msg_t msg = { .kind = LED_EV_CLEAR_ALL_BASE };
    post_event(&msg);
}

/* ========================================================================== */
/*  Overlay                                                                   */
/* ========================================================================== */

void led_manager_notify(ws2812b_color_t color,
                        led_pattern_t pat,
                        uint32_t blink_count,
                        uint32_t duration_ms)
{
    if (duration_ms == 0) duration_ms = 150;
    led_event_msg_t msg = {
        .kind        = LED_EV_NOTIFY,
        .color       = color,
        .pattern     = pat,
        .blink_count = blink_count,
        .duration_ms = duration_ms,
    };
    post_event(&msg);
}

void led_manager_clear_overlay(void)
{
    led_event_msg_t msg = { .kind = LED_EV_CLEAR_OVERLAY };
    post_event(&msg);
}

/* ========================================================================== */
/*  Override                                                                  */
/* ========================================================================== */

void led_manager_override_steady(ws2812b_color_t color)
{
    led_event_msg_t msg = { .kind = LED_EV_OVERRIDE_SET, .color = color };
    post_event(&msg);
}

void led_manager_override_release(void)
{
    led_event_msg_t msg = { .kind = LED_EV_OVERRIDE_CLEAR };
    post_event(&msg);
}

/* ========================================================================== */
/*  Subsystem event hooks                                                     */
/* ========================================================================== */

void led_manager_on_power_event(const power_event_t *ev)
{
    if (!ev) return;

    switch (ev->type) {
    case POWER_EVENT_USB_CONNECTED:
        /* Source not yet classified — show generic USB breathe. */
        led_manager_set_base(LED_STATE_USB_CONNECTED);
        break;

    case POWER_EVENT_USB_DISCONNECTED:
        /* Clear every USB/charging base. LOW_BATTERY/CRITICAL may still be
         * asserted if applicable; they remain. */
        led_manager_clear_base(LED_STATE_USB_CONNECTED);
        led_manager_clear_base(LED_STATE_CHARGING);
        led_manager_clear_base(LED_STATE_CHARGING_TOPOFF);
        led_manager_clear_base(LED_STATE_CHARGE_DONE);
        led_manager_notify(WS2812B_COLOR_OFF, LED_PAT_FADE_OUT, 0, 800);
        break;

    case POWER_EVENT_SOURCE_DETECTED:
        /* USB breathe is already active; no visual change, but stash SOC so
         * CHARGING can tune its period. */
        break;

    case POWER_EVENT_CHARGE_START: {
        xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
        uint8_t soc = s_mgr.last_soc_pct;
        xSemaphoreGive(s_mgr.state_lock);
        led_manager_clear_base(LED_STATE_USB_CONNECTED);
        led_manager_clear_base(LED_STATE_CHARGE_DONE);
        if (soc >= 80) {
            led_manager_set_base(LED_STATE_CHARGING_TOPOFF);
        } else {
            led_manager_set_base(LED_STATE_CHARGING);
        }
        break;
    }

    case POWER_EVENT_CHARGE_DONE:
        led_manager_clear_base(LED_STATE_CHARGING);
        led_manager_clear_base(LED_STATE_CHARGING_TOPOFF);
        led_manager_set_base(LED_STATE_CHARGE_DONE);
        break;

    case POWER_EVENT_CHARGE_STOPPED:
        led_manager_clear_base(LED_STATE_CHARGING);
        led_manager_clear_base(LED_STATE_CHARGING_TOPOFF);
        led_manager_notify(WS2812B_COLOR_YELLOW, LED_PAT_BLINK, 1, 600);
        break;

    case POWER_EVENT_LOW_BATTERY:
        xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
        s_mgr.last_soc_pct = ev->battery.soc_pct;
        xSemaphoreGive(s_mgr.state_lock);
        led_manager_set_base(LED_STATE_LOW_BATTERY);
        break;

    case POWER_EVENT_CRITICAL_BATTERY:
        xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
        s_mgr.last_soc_pct = ev->battery.soc_pct;
        xSemaphoreGive(s_mgr.state_lock);
        led_manager_set_base(LED_STATE_CRITICAL_BATTERY);
        break;

    case POWER_EVENT_CHARGER_FAULT:
        led_manager_set_base(LED_STATE_CHARGER_FAULT);
        break;

    case POWER_EVENT_OTG_FAULT:
        led_manager_set_base(LED_STATE_OTG_FAULT);
        break;

    case POWER_EVENT_SNAPSHOT_UPDATED: {
        /* Cache SOC so CHARGING can pick period. If SOC climbed out of
         * LOW/CRITICAL range, retire those states. Also promote
         * CHARGING <-> CHARGING_TOPOFF when crossing 80%. */
        power_snapshot_t snap;
        if (!power_manager_get_snapshot(&snap)) break;

        bool was_charging = false;
        bool was_topoff   = false;
        xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
        s_mgr.last_soc_pct = snap.soc_pct;
        was_charging = (s_mgr.base_asserted_mask & (1U << LED_STATE_CHARGING)) != 0;
        was_topoff   = (s_mgr.base_asserted_mask & (1U << LED_STATE_CHARGING_TOPOFF)) != 0;
        xSemaphoreGive(s_mgr.state_lock);

        if (snap.soc_pct > BMS_LOW_BATTERY_PCT) {
            led_manager_clear_base(LED_STATE_LOW_BATTERY);
        }
        if (snap.soc_pct > BMS_CRITICAL_BATTERY_PCT) {
            led_manager_clear_base(LED_STATE_CRITICAL_BATTERY);
        }

        if ((was_charging || was_topoff) && snap.usb_connected) {
            if (snap.soc_pct >= 80 && was_charging) {
                led_manager_clear_base(LED_STATE_CHARGING);
                led_manager_set_base(LED_STATE_CHARGING_TOPOFF);
            } else if (snap.soc_pct < 80 && was_topoff) {
                led_manager_clear_base(LED_STATE_CHARGING_TOPOFF);
                led_manager_set_base(LED_STATE_CHARGING);
            }
        }
        break;
    }

    default:
        break;
    }
}

void led_manager_on_ble_event(const ble_event_t *ev)
{
    if (!ev) return;

    switch (ev->type) {
    case BLE_EVENT_ADV_STARTED:
        led_manager_set_base(LED_STATE_BLE_ADVERTISING);
        break;

    case BLE_EVENT_ADV_STOPPED:
        led_manager_clear_base(LED_STATE_BLE_ADVERTISING);
        break;

    case BLE_EVENT_CONNECTED:
        led_manager_clear_base(LED_STATE_BLE_ADVERTISING);
        led_manager_set_base(LED_STATE_BLE_CONNECTED_IDLE);
        led_manager_notify(WS2812B_COLOR_BLUE, LED_PAT_BLINK, 2, 500);
        break;

    case BLE_EVENT_DISCONNECTED:
        led_manager_clear_base(LED_STATE_BLE_CONNECTED_IDLE);
        led_manager_notify(WS2812B_COLOR_BLUE, LED_PAT_BLINK, 1, 300);
        break;

    case BLE_EVENT_PAIRED:
        led_manager_notify(WS2812B_COLOR_COOL_WHITE, LED_PAT_BLINK, 3, 600);
        break;

    case BLE_EVENT_PAIRING_FAILED:
        led_manager_notify(WS2812B_COLOR_VIOLET, LED_PAT_BLINK, 3, 450);
        break;

    case BLE_EVENT_TIME_SYNCED:
        /* Debug-only — gated internally by verbosity. */
        led_manager_notify(WS2812B_COLOR_WHITE, LED_PAT_STEADY, 0, 80);
        break;

    case BLE_EVENT_MTU_CHANGED:
    default:
        /* No visual. */
        break;
    }
}

void led_manager_on_radar_state(a121_vitals_state_t state,
                                bool result_ready,
                                float hr_confidence)
{
    switch (state) {
    case A121_VITALS_STATE_INIT:
    case A121_VITALS_STATE_NO_PRESENCE:
        led_manager_clear_base(LED_STATE_RADAR_INTRA);
        led_manager_clear_base(LED_STATE_RADAR_DISTANCE);
        led_manager_clear_base(LED_STATE_RADAR_ESTIMATING);
        led_manager_clear_base(LED_STATE_VITALS_LOCKED);
        break;

    case A121_VITALS_STATE_INTRA_PRESENCE:
        led_manager_clear_base(LED_STATE_RADAR_DISTANCE);
        led_manager_clear_base(LED_STATE_RADAR_ESTIMATING);
        led_manager_clear_base(LED_STATE_VITALS_LOCKED);
        led_manager_set_base(LED_STATE_RADAR_INTRA);
        break;

    case A121_VITALS_STATE_DETERMINE_DISTANCE:
        led_manager_clear_base(LED_STATE_RADAR_INTRA);
        led_manager_clear_base(LED_STATE_RADAR_ESTIMATING);
        led_manager_clear_base(LED_STATE_VITALS_LOCKED);
        led_manager_set_base(LED_STATE_RADAR_DISTANCE);
        break;

    case A121_VITALS_STATE_ESTIMATE_BREATHING_RATE:
        led_manager_clear_base(LED_STATE_RADAR_INTRA);
        led_manager_clear_base(LED_STATE_RADAR_DISTANCE);
        if (result_ready && hr_confidence > 50.0f) {
            led_manager_clear_base(LED_STATE_RADAR_ESTIMATING);
            led_manager_set_base(LED_STATE_VITALS_LOCKED);
        } else {
            led_manager_clear_base(LED_STATE_VITALS_LOCKED);
            led_manager_set_base(LED_STATE_RADAR_ESTIMATING);
        }
        break;

    default:
        break;
    }
}

void led_manager_on_imu_motion(void)
{
    /* FULL-verbosity transient. The LED task will drop it if verbosity < FULL. */
    led_manager_notify(WS2812B_COLOR_LIME, LED_PAT_BLINK, 1, 120);
}

/* ========================================================================== */
/*  Boot sequence                                                             */
/* ========================================================================== */

void led_manager_boot_stage_ok(const char *tag)
{
    (void)tag;
    led_manager_set_base(LED_STATE_BOOT);
    led_manager_notify(WS2812B_COLOR_DIM_WHITE, LED_PAT_BLINK, 1, 160);
}

void led_manager_boot_stage_fail(const char *tag)
{
    ESP_LOGE(TAG, "Boot stage '%s' failed", tag ? tag : "?");
    led_manager_set_base(LED_STATE_BOOT_FAIL);
    led_manager_notify(WS2812B_COLOR_RED, LED_PAT_BLINK, 3, 900);
}

void led_manager_boot_complete(void)
{
    led_manager_clear_base(LED_STATE_BOOT);

    xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
    bool boot_failed =
        (s_mgr.base_asserted_mask & (1U << LED_STATE_BOOT_FAIL)) != 0;
    xSemaphoreGive(s_mgr.state_lock);

    if (!boot_failed) {
        led_manager_notify(WS2812B_COLOR_GREEN, LED_PAT_STEADY, 0, 1000);
    }
}

/* ========================================================================== */
/*  Internal: queue posting                                                   */
/* ========================================================================== */

static void post_event(const led_event_msg_t *msg)
{
    if (!s_mgr.initialized || !s_mgr.queue) return;
    /* If the queue is full, drop — LED feedback is non-critical telemetry.
     * Block briefly to smooth over bursts. */
    if (xQueueSend(s_mgr.queue, msg, pdMS_TO_TICKS(20)) != pdPASS) {
        ESP_LOGW(TAG, "event queue full, dropping kind=%d", (int)msg->kind);
    }
}

/* ========================================================================== */
/*  Internal: base resolution                                                 */
/* ========================================================================== */

static led_state_t resolve_base_from_mask(uint32_t mask)
{
    /* Highest enum value with a set bit wins. */
    for (int s = (int)LED_STATE__COUNT - 1; s >= 0; --s) {
        if (mask & (1U << s)) {
            /* Skip states below current verbosity. */
            if (s_mgr.verbosity >= s_state_def[s].min_verbosity &&
                s_state_def[s].render) {
                return (led_state_t)s;
            }
            /* If it has render=false but matches verbosity, still consider
             * it "claimed" — but render nothing. For simplicity, fall
             * through to LED_STATE_OFF. */
        }
    }
    return LED_STATE_OFF;
}

static void apply_verbosity_gate(void)
{
    /* Called after a verbosity change. If the active base state is now below
     * threshold, fall back. Also drop overlay/override if verbosity is OFF. */
    if (s_mgr.verbosity == LED_VERBOSITY_OFF) {
        s_mgr.overlay_active  = false;
        /* Override stays — it's button-owned hard feedback. Even at OFF the
         * user deliberately pressed the button, so a STUCK red must show. */
    }
    s_mgr.base_active = resolve_base_from_mask(s_mgr.base_asserted_mask);
}

/* ========================================================================== */
/*  Internal: render                                                          */
/* ========================================================================== */

static ws2812b_color_t apply_brightness(ws2812b_color_t c, uint8_t scale)
{
    c.r = u8mul(c.r, scale);
    c.g = u8mul(c.g, scale);
    c.b = u8mul(c.b, scale);
    return c;
}

static ws2812b_color_t compute_base_pixel(uint32_t tick)
{
    const led_state_def_t *def = &s_state_def[s_mgr.base_active];
    if (!def->render) return WS2812B_COLOR_OFF;

    uint32_t elapsed_ms = tick * LED_MANAGER_TICK_MS;

    switch (def->pattern) {
    case LED_PAT_OFF:
        return WS2812B_COLOR_OFF;

    case LED_PAT_STEADY:
        return def->color;

    case LED_PAT_BREATHE: {
        uint32_t period = def->period_ms ? def->period_ms : 2000;
        /* For CHARGING specifically, scale period by SOC so it breathes
         * faster as the battery fills — visual "progress". */
        if (s_mgr.base_active == LED_STATE_CHARGING ||
            s_mgr.base_active == LED_STATE_CHARGING_TOPOFF) {
            uint8_t soc = s_mgr.last_soc_pct;
            if (soc > 100) soc = 100;
            /* 3000 ms at 0%, 1000 ms at 100%, linear. */
            period = 3000U - ((uint32_t)soc * 20U);
        }
        uint32_t phase = (elapsed_ms % period) * 64U / period; /* 0..63 */
        if (phase >= 64) phase = 63;
        uint8_t lvl = s_sine_lut[phase];
        return apply_brightness(def->color, lvl);
    }

    case LED_PAT_BLINK: {
        uint32_t period = def->period_ms ? def->period_ms : 500;
        uint32_t phase = elapsed_ms % period;
        ws2812b_color_t off = WS2812B_COLOR_OFF;
        return (phase < period / 2) ? def->color : off;
    }

    case LED_PAT_STROBE: {
        uint32_t period = def->period_ms ? def->period_ms : 100;
        uint32_t phase = elapsed_ms % period;
        ws2812b_color_t off = WS2812B_COLOR_OFF;
        return (phase < period / 2) ? def->color : off;
    }

    case LED_PAT_DOUBLE_BLINK_REPEAT: {
        /* Pattern: ON (150) OFF (150) ON (150) OFF (150) rest (2400). */
        uint32_t phase = elapsed_ms % 3000U;
        if      (phase <  150U) return def->color;
        else if (phase <  300U) return WS2812B_COLOR_OFF;
        else if (phase <  450U) return def->color;
        else                    return WS2812B_COLOR_OFF;
    }

    case LED_PAT_FADE_OUT:
    default:
        return def->color;
    }
}

static ws2812b_color_t compute_overlay_pixel(uint32_t tick, bool *done)
{
    *done = false;
    uint32_t elapsed_ms =
        (uint32_t)((tick - (s_mgr.overlay_start_tick)) * LED_MANAGER_TICK_MS);

    if (elapsed_ms >= s_mgr.overlay_duration_ms) {
        *done = true;
        return WS2812B_COLOR_OFF;
    }

    switch (s_mgr.overlay_pattern) {
    case LED_PAT_OFF:
        *done = true;
        return WS2812B_COLOR_OFF;

    case LED_PAT_STEADY:
        return s_mgr.overlay_color;

    case LED_PAT_BLINK: {
        /* Divide duration_ms into (blink_count*2) slots: on/off repeating. */
        uint32_t n = s_mgr.overlay_blink_count ? s_mgr.overlay_blink_count : 1;
        uint32_t slot = s_mgr.overlay_duration_ms / (n * 2U);
        if (slot == 0) slot = 1;
        uint32_t idx = elapsed_ms / slot;
        ws2812b_color_t off = WS2812B_COLOR_OFF;
        return (idx & 1U) ? off : s_mgr.overlay_color;
    }

    case LED_PAT_STROBE: {
        uint32_t slot = 60; /* ms */
        uint32_t idx = elapsed_ms / slot;
        uint32_t max_idx = s_mgr.overlay_blink_count * 2U;
        if (max_idx && idx >= max_idx) {
            *done = true;
            return WS2812B_COLOR_OFF;
        }
        ws2812b_color_t off = WS2812B_COLOR_OFF;
        return (idx & 1U) ? off : s_mgr.overlay_color;
    }

    case LED_PAT_FADE_OUT: {
        uint32_t total = s_mgr.overlay_duration_ms;
        if (total == 0) { *done = true; return WS2812B_COLOR_OFF; }
        uint32_t remaining = total - elapsed_ms;
        uint8_t scale = (uint8_t)((remaining * 255U) / total);
        return apply_brightness(s_mgr.overlay_color, scale);
    }

    case LED_PAT_BREATHE: {
        uint32_t period = 1500; /* Overlays use a fixed nice period. */
        uint32_t phase = (elapsed_ms % period) * 64U / period;
        if (phase >= 64) phase = 63;
        return apply_brightness(s_mgr.overlay_color, s_sine_lut[phase]);
    }

    default:
        return s_mgr.overlay_color;
    }
}

static void render_tick(void)
{
    ws2812b_color_t out;
    bool overlay_done = false;

    if (s_mgr.verbosity == LED_VERBOSITY_OFF && !s_mgr.override_active) {
        out = WS2812B_COLOR_OFF;
    } else if (s_mgr.override_active) {
        out = s_mgr.override_color;
    } else if (s_mgr.overlay_active) {
        out = compute_overlay_pixel(s_mgr.tick_count, &overlay_done);
    } else {
        out = compute_base_pixel(s_mgr.tick_count);
    }

    /* Global brightness scaling — except for override (button intent is
     * literal) and OFF. */
    if (!s_mgr.override_active &&
        !(out.r == 0 && out.g == 0 && out.b == 0) &&
        s_mgr.max_brightness < 255) {
        out = apply_brightness(out, s_mgr.max_brightness);
    }

    if (!s_mgr.last_rendered_valid ||
        out.r != s_mgr.last_rendered.r ||
        out.g != s_mgr.last_rendered.g ||
        out.b != s_mgr.last_rendered.b) {
        ws2812b_fill_color(out);
        ws2812b_refresh();
        s_mgr.last_rendered       = out;
        s_mgr.last_rendered_valid = true;
    }

    if (overlay_done) {
        s_mgr.overlay_active = false;
    }
}

/* ========================================================================== */
/*  Internal: event handling on the task                                      */
/* ========================================================================== */

static void handle_event_locked(const led_event_msg_t *msg)
{
    switch (msg->kind) {
    case LED_EV_SET_BASE:
        s_mgr.base_asserted_mask |= (1U << msg->state);
        s_mgr.base_active = resolve_base_from_mask(s_mgr.base_asserted_mask);
        break;

    case LED_EV_CLEAR_BASE:
        /* Never clear the OFF floor. */
        if (msg->state != LED_STATE_OFF) {
            s_mgr.base_asserted_mask &= ~(1U << msg->state);
        }
        s_mgr.base_active = resolve_base_from_mask(s_mgr.base_asserted_mask);
        break;

    case LED_EV_CLEAR_ALL_BASE:
        s_mgr.base_asserted_mask = (1U << LED_STATE_OFF);
        s_mgr.base_active = LED_STATE_OFF;
        break;

    case LED_EV_NOTIFY:
        s_mgr.overlay_color       = msg->color;
        s_mgr.overlay_pattern     = msg->pattern;
        s_mgr.overlay_blink_count = msg->blink_count;
        s_mgr.overlay_duration_ms = msg->duration_ms;
        s_mgr.overlay_start_tick  = s_mgr.tick_count;
        s_mgr.overlay_active      = (s_mgr.verbosity != LED_VERBOSITY_OFF);
        break;

    case LED_EV_CLEAR_OVERLAY:
        s_mgr.overlay_active = false;
        break;

    case LED_EV_OVERRIDE_SET:
        s_mgr.override_color  = msg->color;
        s_mgr.override_active = true;
        break;

    case LED_EV_OVERRIDE_CLEAR:
        s_mgr.override_active = false;
        break;

    case LED_EV_SET_VERBOSITY:
        if (s_mgr.verbosity != (led_verbosity_t)msg->u8) {
            s_mgr.verbosity = (led_verbosity_t)msg->u8;
            apply_verbosity_gate();
            save_nvs();
        }
        break;

    case LED_EV_SET_BRIGHTNESS:
        if (s_mgr.max_brightness != msg->u8) {
            s_mgr.max_brightness = msg->u8;
            save_nvs();
        }
        break;

    case LED_EV_SHUTDOWN:
        s_mgr.task_running = false;
        break;

    default:
        break;
    }
}

static void led_task(void *arg)
{
    (void)arg;
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t tick_ticks = pdMS_TO_TICKS(LED_MANAGER_TICK_MS);

    /* Start from a known blanked state. */
    ws2812b_fill_color(WS2812B_COLOR_OFF);
    ws2812b_refresh();
    s_mgr.last_rendered       = WS2812B_COLOR_OFF;
    s_mgr.last_rendered_valid = true;

    while (s_mgr.task_running) {
        /* Drain all pending events (non-blocking) before the next render. */
        led_event_msg_t msg;
        while (xQueueReceive(s_mgr.queue, &msg, 0) == pdTRUE) {
            xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
            handle_event_locked(&msg);
            xSemaphoreGive(s_mgr.state_lock);
            if (msg.kind == LED_EV_SHUTDOWN) goto task_exit;
        }

        xSemaphoreTake(s_mgr.state_lock, portMAX_DELAY);
        render_tick();
        s_mgr.tick_count++;
        xSemaphoreGive(s_mgr.state_lock);

        vTaskDelayUntil(&next_wake, tick_ticks);
    }

task_exit:
    ws2812b_fill_color(WS2812B_COLOR_OFF);
    ws2812b_refresh();
    s_mgr.task_running = false;
    vTaskDelete(NULL);
}

/* ========================================================================== */
/*  NVS                                                                       */
/* ========================================================================== */

static esp_err_t load_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LED_MANAGER_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t v;
    if (nvs_get_u8(h, "verb", &v) == ESP_OK) {
        if (v > LED_VERBOSITY_DEBUG) v = LED_VERBOSITY_DEBUG;
        s_mgr.verbosity = (led_verbosity_t)v;
    }

    uint8_t b;
    if (nvs_get_u8(h, "bright", &b) == ESP_OK) {
        s_mgr.max_brightness = b;
    }

    nvs_close(h);
    return ESP_OK;
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(LED_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    (void)nvs_set_u8(h, "verb",   (uint8_t)s_mgr.verbosity);
    (void)nvs_set_u8(h, "bright", s_mgr.max_brightness);
    (void)nvs_commit(h);
    nvs_close(h);
}
