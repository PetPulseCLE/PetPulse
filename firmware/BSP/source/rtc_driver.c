/**
 * @file rtc_driver.c
 * @brief RTC driver implementation for ESP32-S3.
 *
 * The ESP32-S3 RTC keeps time across light-sleep and deep-sleep. When backed
 * by the external 32.768 kHz crystal on XTAL_32K_P/N (GPIO15/16) via
 * CONFIG_RTC_CLK_SRC_EXT_CRYS, drift is typically < 20 ppm (~1.7 s/day).
 *
 * This driver wraps the POSIX time functions (settimeofday / gettimeofday)
 * and adds timezone persistence, BLE CTS parsing, and convenience helpers.
 */

#include "rtc_driver.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG "RTC"

/* Maximum POSIX TZ string length we'll store */
#define TZ_MAX_LEN  64

/* ========================================================================== */
/*  Internal State                                                            */
/* ========================================================================== */

static struct {
    bool     initialized;
    bool     time_set;       /**< True after first explicit set */
    char     tz_str[TZ_MAX_LEN];
} s_rtc = {
    .initialized = false,
    .time_set    = false,
    .tz_str      = RTC_DEFAULT_TIMEZONE,
};

/* ========================================================================== */
/*  Helpers                                                                   */
/* ========================================================================== */

/** Convert rtc_datetime_t to struct tm (UTC). */
static void datetime_to_tm(const rtc_datetime_t *dt, struct tm *tm_out)
{
    memset(tm_out, 0, sizeof(*tm_out));
    tm_out->tm_year = dt->year - 1900;
    tm_out->tm_mon  = dt->month - 1;       /* struct tm: 0–11 */
    tm_out->tm_mday = dt->day;
    tm_out->tm_hour = dt->hour;
    tm_out->tm_min  = dt->minute;
    tm_out->tm_sec  = dt->second;
    tm_out->tm_isdst = -1;
}

/** Convert struct tm to rtc_datetime_t. */
static void tm_to_datetime(const struct tm *tm_in, rtc_datetime_t *dt)
{
    dt->year        = (uint16_t)(tm_in->tm_year + 1900);
    dt->month       = (uint8_t)(tm_in->tm_mon + 1);
    dt->day         = (uint8_t)tm_in->tm_mday;
    dt->hour        = (uint8_t)tm_in->tm_hour;
    dt->minute      = (uint8_t)tm_in->tm_min;
    dt->second      = (uint8_t)tm_in->tm_sec;
    dt->day_of_week = (uint8_t)tm_in->tm_wday;   /* 0 = Sunday */
}

/** Apply the current timezone to the C library. */
static void apply_timezone(void)
{
    setenv("TZ", s_rtc.tz_str, 1);
    tzset();
}

/** Save timezone to NVS. */
static esp_err_t tz_save_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RTC_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, RTC_NVS_KEY_TZ, s_rtc.tz_str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

/** Load timezone from NVS. */
static esp_err_t tz_load_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RTC_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    size_t len = sizeof(s_rtc.tz_str);
    err = nvs_get_str(nvs, RTC_NVS_KEY_TZ, s_rtc.tz_str, &len);
    nvs_close(nvs);
    return err;
}

/** Validate basic date-time sanity. */
static bool datetime_is_valid(const rtc_datetime_t *dt)
{
    if (dt->year < 2000 || dt->year > 2099) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->day < 1 || dt->day > 31) return false;
    if (dt->hour > 23) return false;
    if (dt->minute > 59) return false;
    if (dt->second > 59) return false;
    return true;
}

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

esp_err_t rtc_driver_init(void)
{
    if (s_rtc.initialized) {
        return ESP_OK;
    }

    /* Attempt to restore timezone from NVS */
    esp_err_t err = tz_load_nvs();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved timezone, using default: %s", s_rtc.tz_str);
    } else {
        ESP_LOGI(TAG, "Restored timezone: %s", s_rtc.tz_str);
    }

    apply_timezone();

    /* Check if the system clock is already set (survives deep-sleep on ESP32-S3
     * with external crystal). If time > Jan 1 2024, assume it's valid. */
    time_t now = time(NULL);
    if (now > 1704067200) {  /* 2024-01-01 00:00:00 UTC */
        s_rtc.time_set = true;
        struct tm tm;
        localtime_r(&now, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        ESP_LOGI(TAG, "RTC time already set: %s (local)", buf);
    } else {
        ESP_LOGW(TAG, "RTC time not set (reads epoch). Waiting for BLE sync.");
    }

    s_rtc.initialized = true;
    ESP_LOGI(TAG, "RTC driver initialized (32.768 kHz external crystal)");
    return ESP_OK;
}

/* ========================================================================== */
/*  Time Get / Set                                                            */
/* ========================================================================== */

time_t rtc_get_timestamp(void)
{
    return time(NULL);
}

esp_err_t rtc_get_datetime(rtc_datetime_t *dt)
{
    if (!dt) return ESP_ERR_INVALID_ARG;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm_to_datetime(&tm, dt);
    return ESP_OK;
}

esp_err_t rtc_get_datetime_utc(rtc_datetime_t *dt)
{
    if (!dt) return ESP_ERR_INVALID_ARG;

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    tm_to_datetime(&tm, dt);
    return ESP_OK;
}

esp_err_t rtc_set_timestamp(time_t timestamp)
{
    struct timeval tv = {
        .tv_sec = timestamp,
        .tv_usec = 0,
    };
    int ret = settimeofday(&tv, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "settimeofday failed: %d", ret);
        return ESP_FAIL;
    }

    s_rtc.time_set = true;

    struct tm tm;
    localtime_r(&timestamp, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "RTC set to: %s (local)", buf);

    return ESP_OK;
}

esp_err_t rtc_set_datetime_utc(const rtc_datetime_t *dt)
{
    if (!dt) return ESP_ERR_INVALID_ARG;
    if (!datetime_is_valid(dt)) {
        ESP_LOGE(TAG, "Invalid datetime: %04u-%02u-%02u %02u:%02u:%02u",
                 dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second);
        return ESP_ERR_INVALID_ARG;
    }

    struct tm tm;
    datetime_to_tm(dt, &tm);

    /* Use timegm (or mktime with TZ=UTC) to convert UTC struct tm → time_t */
    /* setenv/tzset dance to ensure mktime treats it as UTC */
    char old_tz[TZ_MAX_LEN];
    const char *env_tz = getenv("TZ");
    if (env_tz) {
        strncpy(old_tz, env_tz, sizeof(old_tz) - 1);
        old_tz[sizeof(old_tz) - 1] = '\0';
    } else {
        old_tz[0] = '\0';
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);

    /* Restore original TZ */
    if (old_tz[0]) {
        setenv("TZ", old_tz, 1);
    } else {
        setenv("TZ", s_rtc.tz_str, 1);
    }
    tzset();

    if (t == (time_t)-1) {
        ESP_LOGE(TAG, "mktime failed");
        return ESP_FAIL;
    }

    return rtc_set_timestamp(t);
}

/* ========================================================================== */
/*  BLE CTS                                                                   */
/* ========================================================================== */

esp_err_t rtc_set_from_ble_cts(const uint8_t *data, size_t data_len)
{
    if (!data || data_len < 10) {
        ESP_LOGE(TAG, "CTS payload too short (%d bytes, need 10)", (int)data_len);
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * BLE Current Time characteristic (0x2A2B) layout:
     *  [0-1] year   (uint16 LE)
     *  [2]   month  (1–12, 0 = unknown)
     *  [3]   day    (1–31, 0 = unknown)
     *  [4]   hour   (0–23)
     *  [5]   minute (0–59)
     *  [6]   second (0–59)
     *  [7]   day_of_week (1 = Monday … 7 = Sunday, 0 = unknown)
     *  [8]   fractions256 (1/256th of a second)
     *  [9]   adjust_reason (bitmask)
     */
    rtc_datetime_t dt = {
        .year   = (uint16_t)(data[0] | (data[1] << 8)),
        .month  = data[2],
        .day    = data[3],
        .hour   = data[4],
        .minute = data[5],
        .second = data[6],
    };

    /* CTS day_of_week: 1=Mon…7=Sun → our format: 0=Sun…6=Sat */
    if (data[7] >= 1 && data[7] <= 7) {
        dt.day_of_week = (data[7] == 7) ? 0 : data[7];
    }

    ESP_LOGI(TAG, "BLE CTS sync: %04u-%02u-%02u %02u:%02u:%02u.%03u",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
             (unsigned)((data[8] * 1000U) / 256));

    /* Convert to UTC timestamp, then apply fractions256 as microseconds */
    struct tm tm;
    datetime_to_tm(&dt, &tm);

    /* mktime in UTC context */
    char old_tz[TZ_MAX_LEN];
    const char *env_tz = getenv("TZ");
    if (env_tz) {
        strncpy(old_tz, env_tz, sizeof(old_tz) - 1);
        old_tz[sizeof(old_tz) - 1] = '\0';
    } else {
        old_tz[0] = '\0';
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);

    /* Restore original TZ */
    if (old_tz[0]) {
        setenv("TZ", old_tz, 1);
    } else {
        setenv("TZ", s_rtc.tz_str, 1);
    }
    tzset();

    if (t == (time_t)-1) {
        ESP_LOGE(TAG, "mktime failed");
        return ESP_FAIL;
    }

    /* Apply fractions256: convert 1/256s to microseconds */
    uint32_t frac_us = (uint32_t)data[8] * 1000000U / 256U;

    struct timeval tv = {
        .tv_sec  = t,
        .tv_usec = (suseconds_t)frac_us,
    };
    int ret = settimeofday(&tv, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "settimeofday failed: %d", ret);
        return ESP_FAIL;
    }

    s_rtc.time_set = true;
    return ESP_OK;
}

esp_err_t rtc_get_ble_cts(uint8_t *buf, size_t *len)
{
    if (!buf || !len) return ESP_ERR_INVALID_ARG;

    rtc_datetime_t dt;
    esp_err_t err = rtc_get_datetime_utc(&dt);
    if (err != ESP_OK) return err;

    /* Build 10-byte CTS payload */
    buf[0] = (uint8_t)(dt.year & 0xFF);
    buf[1] = (uint8_t)(dt.year >> 8);
    buf[2] = dt.month;
    buf[3] = dt.day;
    buf[4] = dt.hour;
    buf[5] = dt.minute;
    buf[6] = dt.second;

    /* Our day_of_week: 0=Sun…6=Sat → CTS: 1=Mon…7=Sun */
    if (dt.day_of_week == 0) {
        buf[7] = 7;   /* Sunday */
    } else {
        buf[7] = dt.day_of_week;
    }

    buf[8] = 0;   /* fractions256: filled below */
    buf[9] = 0;   /* adjust_reason: 0 */

    /* Populate fractions256 from current sub-second time */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    buf[8] = (uint8_t)((uint32_t)tv.tv_usec * 256U / 1000000U);

    *len = 10;
    return ESP_OK;
}

/* ========================================================================== */
/*  Timezone                                                                  */
/* ========================================================================== */

esp_err_t rtc_set_timezone(const char *tz_str)
{
    if (!tz_str || strlen(tz_str) >= TZ_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_rtc.tz_str, tz_str, TZ_MAX_LEN - 1);
    s_rtc.tz_str[TZ_MAX_LEN - 1] = '\0';

    apply_timezone();

    esp_err_t err = tz_save_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save timezone to NVS: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Timezone set: %s", s_rtc.tz_str);
    return ESP_OK;
}

esp_err_t rtc_get_timezone(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;

    strncpy(buf, s_rtc.tz_str, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return ESP_OK;
}

/* ========================================================================== */
/*  Utilities                                                                 */
/* ========================================================================== */

bool rtc_is_time_set(void)
{
    return s_rtc.time_set;
}

uint32_t rtc_get_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

esp_err_t rtc_strftime(char *buf, size_t buf_len, const char *fmt)
{
    if (!buf || !fmt || buf_len == 0) return ESP_ERR_INVALID_ARG;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    size_t ret = strftime(buf, buf_len, fmt, &tm);
    if (ret == 0) {
        buf[0] = '\0';
        return ESP_FAIL;
    }

    return ESP_OK;
}
