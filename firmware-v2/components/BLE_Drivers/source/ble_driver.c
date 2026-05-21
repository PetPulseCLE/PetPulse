/**
 * @file ble_driver.c
 * @brief NimBLE BLE driver for ESP32-S3.
 *
 * Full BLE peripheral implementation:
 *  - GAP advertising (connectable, scannable, undirected)
 *  - GATT server with Device Information Service (DIS)
 *  - Security Manager: Just Works + OOB, bonding, LE Secure Connections
 *  - Connection management, MTU negotiation
 *  - OOB TK generation for NFC tap-to-pair
 *
 * Compatible with iPhone (iOS 14+) and Android (4.1+).
 */

#include "ble_driver.h"
#include "rtc_driver.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_bt.h"

/* NimBLE includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "BLE"

/* ========================================================================== */
/*  Internal State                                                            */
/* ========================================================================== */

static struct {
    bool                initialized;
    bool                advertising;
    bool                synced;         /**< Host-controller sync'd */
    uint16_t            conn_handle;    /**< 0xFFFF = none */
    ble_event_cb_t      event_cb;
    void               *user_data;
    uint16_t            appearance;
    bool                bonding;
    bool                mitm;
    char                device_name[BLE_DEVICE_NAME_MAX_LEN + 1];
    uint8_t             own_addr[6];
    uint8_t             own_addr_type;
    uint8_t             oob_tk[16];     /**< Current OOB TK (regenerated each session) */
    bool                oob_tk_valid;
    uint16_t            bas_chr_val_handle;  /**< ATT handle for BAS Battery Level (notify) */
    uint8_t             bas_current_soc;     /**< Actual value for read requests */
    uint8_t             bas_last_notified_soc; /**< Last notified SOC to suppress duplicates */

    /* Custom PetPulse Profile Handlers */
    uint16_t            vitals_val_handle;
    uint16_t            raw_val_handle;
    uint16_t            activity_val_handle;
    uint16_t            mode_val_handle;
    uint16_t            auth_ping_val_handle;
    uint16_t            env_val_handle;
    uint16_t            agg_val_handle;

    /* Custom PetPulse Profile Data Caches */
    ble_vitals_t         vitals_cache;
    ble_raw_t            raw_cache;
    ble_activity_t       activity_cache;
    ble_env_t            env_cache;
    ble_aggregated_all_t agg_cache;
    ble_mode_t           mode_cache;

    /* Battery Profile Handlers & Data Caches */
    uint16_t            batt_lvl_stat_val_handle;
    uint16_t            batt_energy_val_handle;
    uint16_t            batt_time_val_handle;
    uint16_t            batt_health_val_handle;

    ble_batt_level_stat_t batt_lvl_stat_cache;
    ble_batt_energy_stat_t batt_energy_cache;
    ble_batt_time_stat_t batt_time_cache;
    ble_batt_health_stat_t batt_health_cache;
} s_ble = {
    .conn_handle = 0xFFFF,
    .mode_cache = BLE_MODE_LIVE,
    .bas_last_notified_soc = 0xFF,
};

/* Forward declarations */
static int  ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_host_task(void *param);
static void ble_fire_event(const ble_event_t *evt);

/* Map a GATT value handle to a human-readable name for diagnostic logs.
 * Covers every notifiable/indicatable characteristic registered by this
 * driver. Returns "(unknown)" for handles owned by ble_svc_gap / _gatt
 * built-ins (DIS, CTS, Service Changed) since those are not tracked in
 * s_ble. */
static const char *ble_chr_name_for_handle(uint16_t h)
{
    if (h == s_ble.bas_chr_val_handle)        return "BAS Battery Level (0x2A19)";
    if (h == s_ble.batt_lvl_stat_val_handle)  return "BAS Level Status (0x2BED)";
    if (h == s_ble.batt_energy_val_handle)    return "BAS Energy Status (0x2BF0)";
    if (h == s_ble.batt_time_val_handle)      return "BAS Time Status (0x2BEE)";
    if (h == s_ble.batt_health_val_handle)    return "BAS Health Status (0x2BEA)";
    if (h == s_ble.vitals_val_handle)         return "Vitals";
    if (h == s_ble.raw_val_handle)            return "Motion/Raw";
    if (h == s_ble.activity_val_handle)       return "Motion/Activity";
    if (h == s_ble.mode_val_handle)           return "Motion/Mode";
    if (h == s_ble.auth_ping_val_handle)      return "Motion/AuthPing";
    if (h == s_ble.env_val_handle)            return "Environmental";
    if (h == s_ble.agg_val_handle)            return "Aggregated";
    return "(unknown)";
}

static const char *ble_subscribe_reason_str(uint8_t reason)
{
    switch (reason) {
        case BLE_GAP_SUBSCRIBE_REASON_WRITE:   return "write";
        case BLE_GAP_SUBSCRIBE_REASON_TERM:    return "term";
        case BLE_GAP_SUBSCRIBE_REASON_RESTORE: return "restore";
        default:                                return "?";
    }
}

/* Encode millivolts as IEEE-11073 16-bit SFLOAT (medfloat16) in volts.
 * SFLOAT layout: [exponent:4 signed | mantissa:12 signed], value = mantissa * 10^exponent.
 * Use exponent = -2 and mantissa = round(mv/10) → 0.01 V resolution, well within the
 * 12-bit signed mantissa range for any realistic battery voltage. */
static uint16_t medfloat16_volts_from_mv(uint16_t mv)
{
    int32_t mantissa = (int32_t)((mv + 5u) / 10u); /* round-to-nearest at 10 mV */
    /* Reserve the SFLOAT sentinel slots: 0x07FE=+INF, 0x07FF=NaN — cap usable +mantissa at 2045. */
    if (mantissa > 2045) mantissa = 2045;
    const int8_t exponent = -2;
    const uint16_t mant12 = (uint16_t)mantissa & 0x0FFFu;
    const uint16_t exp4   = (uint16_t)exponent & 0x000Fu;
    return (uint16_t)((exp4 << 12) | mant12);
}

/* ========================================================================== */
/*  GATT Services                                                             */
/* ========================================================================== */

/*
 * Device Information Service (0x180A) — read-only characteristics:
 *   - Manufacturer Name   (0x2A29)
 *   - Model Number         (0x2A24)
 *   - Firmware Revision    (0x2A26)
 */

#define DIS_MANUFACTURER  "Team PetPulse"
#define DIS_MODEL         "DEV-001"
#define DIS_FW_REV        "1.0.0D"

static const char dis_manufacturer[] = DIS_MANUFACTURER;
static const char dis_model[]        = DIS_MODEL;
static const char dis_fw_rev[]       = DIS_FW_REV;

static int
gatt_dis_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *str = (const char *)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, str, strlen(str));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/*
 * Current Time Service (0x1805) — CTS
 *   - Current Time (0x2A2B): read + write-without-response
 *     Read  → returns the ESP32 RTC time in CTS format (10 bytes)
 *     Write → phone pushes its current time to set the RTC
 */
static int
gatt_cts_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[10];
        size_t  len = 0;
        esp_err_t err = rtc_get_ble_cts(buf, &len);
        if (err != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        int rc = os_mbuf_append(ctxt->om, buf, len);
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len < 10) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t buf[10];
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        esp_err_t err = rtc_set_from_ble_cts(buf, sizeof(buf));
        if (err == ESP_OK) {
            ble_event_t evt = { .type = BLE_EVENT_TIME_SYNCED };
            ble_fire_event(&evt);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/*
 * Battery Service (0x180F) — BAS
 *   - Battery Level (0x2A19): read + notify
 *     Read  → returns cached SOC (0-100) from power_manager_get_snapshot()
 *     Notify → pushed on SOC change via ble_driver_notify_battery_level()
 */
static int
gatt_bas_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    int rc = 0;

    if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(BLE_CHR_BATT_LEVEL_UUID16)) == 0) {
        rc = os_mbuf_append(ctxt->om, &s_ble.bas_current_soc, sizeof(s_ble.bas_current_soc));
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(BLE_CHR_BATT_LEVEL_STAT_UUID16)) == 0) {
        rc = os_mbuf_append(ctxt->om, &s_ble.batt_lvl_stat_cache, sizeof(s_ble.batt_lvl_stat_cache));
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(BLE_CHR_BATT_ENERGY_STAT_UUID16)) == 0) {
        rc = os_mbuf_append(ctxt->om, &s_ble.batt_energy_cache, sizeof(s_ble.batt_energy_cache));
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(BLE_CHR_BATT_TIME_STAT_UUID16)) == 0) {
        rc = os_mbuf_append(ctxt->om, &s_ble.batt_time_cache, sizeof(s_ble.batt_time_cache));
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(BLE_CHR_BATT_HEALTH_STAT_UUID16)) == 0) {
        rc = os_mbuf_append(ctxt->om, &s_ble.batt_health_cache, sizeof(s_ble.batt_health_cache));
    }
    else {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ========================================================================== */
/*  Custom Profile Callbacks                                                  */
/* ========================================================================== */

static int gatt_vitals_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    int rc = os_mbuf_append(ctxt->om, &s_ble.vitals_cache, sizeof(s_ble.vitals_cache));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_motion_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    /* static const to avoid 68 bytes of stack allocation per callback invocation */
    static const ble_uuid128_t raw_uuid  = BLE_UUID128_INIT(BLE_CHR_RAW_UUID128);
    static const ble_uuid128_t act_uuid  = BLE_UUID128_INIT(BLE_CHR_ACTIVITY_UUID128);
    static const ble_uuid128_t mode_uuid = BLE_UUID128_INIT(BLE_CHR_MODE_UUID128);
    static const ble_uuid128_t ping_uuid = BLE_UUID128_INIT(BLE_CHR_AUTH_PING_UUID128);

    if (ble_uuid_cmp(uuid, &raw_uuid.u) == 0) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
        int rc = os_mbuf_append(ctxt->om, &s_ble.raw_cache, sizeof(s_ble.raw_cache));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } 
    else if (ble_uuid_cmp(uuid, &act_uuid.u) == 0) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
        int rc = os_mbuf_append(ctxt->om, &s_ble.activity_cache, sizeof(s_ble.activity_cache));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else if (ble_uuid_cmp(uuid, &mode_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            int rc = os_mbuf_append(ctxt->om, &s_ble.mode_cache, sizeof(s_ble.mode_cache));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint8_t mode_val;
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            int rc = ble_hs_mbuf_to_flat(ctxt->om, &mode_val, sizeof(mode_val), NULL);
            if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
            if (mode_val > BLE_MODE_DEV) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            s_ble.mode_cache = (ble_mode_t)mode_val;
            return 0;
        }
    }
    else if (ble_uuid_cmp(uuid, &ping_uuid.u) == 0) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
        uint8_t ping_resp = 0;
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
            ping_resp = (desc.sec_state.bonded || desc.sec_state.encrypted) ? 1 : 0;
        }
        int rc = os_mbuf_append(ctxt->om, &ping_resp, sizeof(ping_resp));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_env_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    int rc = os_mbuf_append(ctxt->om, &s_ble.env_cache, sizeof(s_ble.env_cache));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_agg_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    int rc = os_mbuf_append(ctxt->om, &s_ble.agg_cache, sizeof(s_ble.agg_cache));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        /* Device Information Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Manufacturer Name */
                .uuid = BLE_UUID16_DECLARE(0x2A29),
                .access_cb = gatt_dis_access,
                .arg = (void *)dis_manufacturer,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* Model Number */
                .uuid = BLE_UUID16_DECLARE(0x2A24),
                .access_cb = gatt_dis_access,
                .arg = (void *)dis_model,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* Firmware Revision */
                .uuid = BLE_UUID16_DECLARE(0x2A26),
                .access_cb = gatt_dis_access,
                .arg = (void *)dis_fw_rev,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }, /* Sentinel */
        },
    },
    {
        /* Current Time Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1805),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Current Time characteristic */
                .uuid = BLE_UUID16_DECLARE(0x2A2B),
                .access_cb = gatt_cts_access,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE_ENC | 
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 }, /* Sentinel */
        },
    },
    {
        /* Battery Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_BATTERY_UUID16), /* 0x180F */
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Legacy 0x2A19 Battery Level is specified as plaintext.
                 * Requiring encryption here forces pairing on any generic
                 * BAS reader (iOS widget, nRF Connect) and compounds
                 * pairing races. Encryption stays on the extended
                 * 0x2BED/0x2BF0/0x2BEE/0x2BEA chars. */
                .uuid = BLE_UUID16_DECLARE(BLE_CHR_BATT_LEVEL_UUID16),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.bas_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CHR_BATT_LEVEL_STAT_UUID16),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.batt_lvl_stat_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CHR_BATT_ENERGY_STAT_UUID16),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.batt_energy_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CHR_BATT_TIME_STAT_UUID16),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.batt_time_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(BLE_CHR_BATT_HEALTH_STAT_UUID16),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.batt_health_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            { 0 }, /* Sentinel */
        },
    },
    {
        /* Vitals Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_SVC_VITALS_UUID128)).u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_VITALS_UUID128)).u,
                .access_cb = gatt_vitals_access,
                .val_handle = &s_ble.vitals_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    {
        /* Motion Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_SVC_MOTION_UUID128)).u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_RAW_UUID128)).u,
                .access_cb = gatt_motion_access,
                .val_handle = &s_ble.raw_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_ACTIVITY_UUID128)).u,
                .access_cb = gatt_motion_access,
                .val_handle = &s_ble.activity_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_MODE_UUID128)).u,
                .access_cb = gatt_motion_access,
                .val_handle = &s_ble.mode_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_AUTH_PING_UUID128)).u,
                .access_cb = gatt_motion_access,
                .val_handle = &s_ble.auth_ping_val_handle,
                .flags =  BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            { 0 },
        },
    },
    {
        /* Environmental Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_SVC_ENVIRO_UUID128)).u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_ENV_UUID128)).u,
                .access_cb = gatt_env_access,
                .val_handle = &s_ble.env_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    {
        /* Aggregated Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_SVC_AGGREGATED_UUID128)).u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(BLE_CHR_AGGREGATED_UUID128)).u,
                .access_cb = gatt_agg_access,
                .val_handle = &s_ble.agg_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 }, /* Sentinel — end of service list */
};

/* ========================================================================== */
/*  Advertising Helpers                                                       */
/* ========================================================================== */

static int ble_start_adv_internal(void)
{
    if (s_ble.advertising) return 0;

    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields  fields     = {0};
    struct ble_hs_adv_fields  rsp_fields = {0};
    int rc;

    size_t name_len = strlen(s_ble.device_name);

    /*
     * ADV packet layout (31 bytes max):
     *   Flags (3) + 128-bit UUID (18) + TX Power (3) = 24 bytes
     *   Remaining 7 bytes: short name if it fits
     *
     * Scan response layout (31 bytes max):
     *   Complete name (2 + name_len) + Appearance (4) + 16-bit UUIDs (8)
     *
     * This layout allows mobile apps to filter scan results on the
     * primary 128-bit service UUID without connecting first.
     */

    /* --- Primary advertising data --- */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* Primary 128-bit service UUID for scan filtering by mobile apps */
    static ble_uuid128_t adv_uuid128[] = {
        BLE_UUID128_INIT(BLE_SVC_VITALS_UUID128),
    };
    fields.uuids128 = adv_uuid128;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 0;  /* More services exist beyond this one */

    /*
     * After flags (3) + tx_power (3) + 128-bit UUID (18) = 24 bytes,
     * we have 7 bytes left (5 usable for name after 2-byte AD overhead).
     * Only include a shortened name in ADV if space permits.
     */
    uint8_t adv_name_max = 5;  /* 31 - 24 (fixed) - 2 (AD overhead) */
    if (name_len <= adv_name_max) {
        fields.name = (uint8_t *)s_ble.device_name;
        fields.name_len = name_len;
        fields.name_is_complete = 1;
    } else if (adv_name_max >= 3) {
        /* Shortened name in ADV, complete name in scan response */
        fields.name = (uint8_t *)s_ble.device_name;
        fields.name_len = adv_name_max;
        fields.name_is_complete = 0;
    }
    /* else: name only in scan response */

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d (ADV data may exceed 31 bytes)", rc);
        return rc;
    }

    /* --- Scan response data --- */
    /* Complete device name (always in scan response for discoverability) */
    rsp_fields.name = (uint8_t *)s_ble.device_name;
    rsp_fields.name_len = name_len;
    rsp_fields.name_is_complete = 1;

    /* Appearance in scan response (4 bytes) */
    rsp_fields.appearance = s_ble.appearance;
    rsp_fields.appearance_is_present = 1;

    /*
     * Standard 16-bit service UUIDs: DIS (0x180A) + CTS (0x1805) + BAS (0x180F).
     * uuids16_is_complete = 0 because custom 128-bit services also exist
     * and the full list is only available via GATT service discovery.
     *
     * Check remaining scan response space:
     *   31 - (2 + name_len) - 4 (appearance) = 25 - name_len bytes available
     *   3x 16-bit UUIDs need 8 bytes (2 overhead + 6 data)
     */
    static ble_uuid16_t svc_uuids[] = {
        BLE_UUID16_INIT(0x180A),
        BLE_UUID16_INIT(0x1805),
        BLE_UUID16_INIT(0x180F),
    };

    if (name_len <= 17) {  /* 25 - 8 = 17: max name length to fit UUIDs */
        rsp_fields.uuids16 = svc_uuids;
        rsp_fields.num_uuids16 = 3;
        rsp_fields.uuids16_is_complete = 0;
    } else {
        ESP_LOGW(TAG, "Device name too long (%d chars), omitting 16-bit UUIDs from scan response", name_len);
    }

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d (scan response may exceed 31 bytes)", rc);
        /* Retry without 16-bit UUIDs if scan response overflows */
        if (rsp_fields.num_uuids16 > 0) {
            ESP_LOGW(TAG, "Retrying scan response without 16-bit UUIDs");
            rsp_fields.uuids16 = NULL;
            rsp_fields.num_uuids16 = 0;
            rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
        }
        if (rc != 0) {
            ESP_LOGE(TAG, "adv_rsp_set_fields retry failed: %d", rc);
            return rc;
        }
    }

    /* --- Advertising parameters --- */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  /* Connectable undirected */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* General discoverable */
    /* Advertising interval: 100 ms typical, good for tap-to-pair responsiveness */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

    rc = ble_gap_adv_start(s_ble.own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        return rc;
    }

    s_ble.advertising = true;
    ESP_LOGI(TAG, "BLE advertising started: \"%s\"", s_ble.device_name);

    ble_event_t evt = {.type = BLE_EVENT_ADV_STARTED};
    ble_fire_event(&evt);

    return 0;
}

/* ========================================================================== */
/*  GAP Event Handler                                                         */
/* ========================================================================== */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE %s; conn_handle=%d",
                 event->connect.status == 0 ? "connected" : "connect failed",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            s_ble.conn_handle = event->connect.conn_handle;
            s_ble.advertising = false;

            /* MTU exchange is always initiated by the central (iPhone and
             * modern Android do this automatically). Pairing is pulled
             * lazily by BLE_GATT_CHR_F_READ_ENC / WRITE_ENC flags on the
             * encrypted characteristics — initiating security from the
             * peripheral here races iOS's own pairing flow and was the
             * primary cause of connect-then-drop events.
             *
             * Connection parameter update is left in, tuned to Apple's
             * Accessory Design Guidelines: interval min multiple of 15 ms,
             * supervision timeout × 2/3 ≥ interval_max × (latency + 1). */
            struct ble_gap_upd_params params = {
                .itvl_min = 12,               /* 15 ms — Apple min */
                .itvl_max = 24,               /* 30 ms */
                .latency  = 0,
                .supervision_timeout = 400,   /* 4 s */
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            (void)ble_gap_update_params(event->connect.conn_handle, &params);

            ble_event_t evt = {
                .type = BLE_EVENT_CONNECTED,
                .conn_handle = event->connect.conn_handle,
            };
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                memcpy(evt.connected.peer_addr, desc.peer_id_addr.val, 6);
                evt.connected.addr_type = desc.peer_id_addr.type;
            } else {
                memset(evt.connected.peer_addr, 0, 6);
                evt.connected.addr_type = 0;
            }
            ble_fire_event(&evt);
        } else {
            /* Connection failed — restart advertising */
            ble_start_adv_internal();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected; reason=0x%02x",
                 event->disconnect.reason);
        s_ble.conn_handle = 0xFFFF;
        s_ble.bas_last_notified_soc = 0xFF; /* Reset notification cache so next connect forces standard bas notify */

        {
            ble_event_t evt = {
                .type = BLE_EVENT_DISCONNECTED,
                .conn_handle = event->disconnect.conn.conn_handle,
            };
            evt.disconnected.reason = event->disconnect.reason;
            ble_fire_event(&evt);
        }

        switch (event->disconnect.reason) {
            case BLE_HS_ETIMEOUT_HCI:
            case BLE_HS_EOS:
            case BLE_HS_ECONTROLLER:
            case BLE_HS_ENOTSYNCED:
                ESP_LOGW(TAG, "Client disconnected - BLE stack reset (reason=%d), waiting for host re-sync", event->disconnect.reason);
                break;
            default:
                ESP_LOGI(TAG, "Client disconnected (reason=%d) - restarting advertising", event->disconnect.reason);
                /* Auto-restart advertising */
                ble_start_adv_internal();
                break;
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d",
                 event->enc_change.status);
        if (event->enc_change.status == 0) {
            ble_event_t evt = {
                .type = BLE_EVENT_PAIRED,
                .conn_handle = event->enc_change.conn_handle,
            };
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                evt.paired.bonded = desc.sec_state.bonded;
                evt.paired.encrypted = desc.sec_state.encrypted;
            } else {
                evt.paired.bonded = false;
                evt.paired.encrypted = false;
            }
            ble_fire_event(&evt);
        } else {
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            
            ble_event_t evt = {
                .type = BLE_EVENT_PAIRING_FAILED,
                .conn_handle = event->enc_change.conn_handle,
            };
            ble_fire_event(&evt);
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Delete old bond and allow re-pairing */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        {
            ble_event_t evt = {
                .type = BLE_EVENT_MTU_CHANGED,
                .conn_handle = event->mtu.conn_handle,
            };
            evt.mtu_changed.mtu = event->mtu.value;
            ble_fire_event(&evt);
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "ADV complete");
        s_ble.advertising = false;
        {
            ble_event_t evt = {.type = BLE_EVENT_ADV_STOPPED};
            ble_fire_event(&evt);
        }
        /* If not connected, restart advertising */
        if (s_ble.conn_handle == 0xFFFF) {
            ble_start_adv_internal();
        }
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        /* Notification/indication TX completed */
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Logged at INFO level so silent-notify failures are diagnosable
         * in production (e.g., client never enables CCCD for BAS → all
         * ble_gatts_chr_updated() calls silently no-op). */
        ESP_LOGI(TAG,
                 "Subscribe: conn=%u attr=%u (%s) notify=%u->%u indicate=%u->%u reason=%s",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 ble_chr_name_for_handle(event->subscribe.attr_handle),
                 event->subscribe.prev_notify, event->subscribe.cur_notify,
                 event->subscribe.prev_indicate, event->subscribe.cur_indicate,
                 ble_subscribe_reason_str(event->subscribe.reason));
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* Promoted from DEBUG so we can confirm whether the central
         * accepted the interval/timeout we requested on connect. */
        if (event->conn_update.status == 0 &&
            ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG,
                     "Connection updated: itvl=%u*1.25ms latency=%u timeout=%u*10ms",
                     desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
        } else {
            ESP_LOGW(TAG, "Connection update failed: status=%d",
                     event->conn_update.status);
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* We use Just Works / OOB — no passkey entry needed */
        ESP_LOGD(TAG, "Passkey action: %d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            /* Provide our OOB TK for pairing */
            struct ble_sm_io pk = {0};
            pk.action = BLE_SM_IOACT_OOB;
            memcpy(pk.oob, s_ble.oob_tk, 16);
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
            /* Just Works — no action needed */
        }
        break;

    default:
        break;
    }

    return 0;
}

/* ========================================================================== */
/*  Host Callbacks                                                            */
/* ========================================================================== */

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
    s_ble.synced = false;
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced");

    /* Use preferred address type */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }

    /* Read our own address */
    rc = ble_hs_id_infer_auto(0, &s_ble.own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "id_infer_auto failed: %d", rc);
        return;
    }

    uint8_t addr[6];
    rc = ble_hs_id_copy_addr(s_ble.own_addr_type, addr, NULL);
    if (rc == 0) {
        memcpy(s_ble.own_addr, addr, 6);
        ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X (type %d)",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                 s_ble.own_addr_type);
    }

    s_ble.synced = true;

    /* Signal Service Changed to bonded peers only when the firmware
     * revision actually changed — iOS caches the GATT database per bond,
     * so stale DBs after an upgrade need invalidation. Firing this on
     * every boot triggered spurious re-discovery churn (and the
     * occasional mid-discovery disconnect) on bonded iOS devices. */
    nvs_handle_t fw_nvs;
    if (nvs_open("ble_drv", NVS_READWRITE, &fw_nvs) == ESP_OK) {
        char stored_rev[sizeof(DIS_FW_REV) + 8] = {0};
        size_t stored_len = sizeof(stored_rev);
        esp_err_t rv = nvs_get_str(fw_nvs, "fw_rev", stored_rev, &stored_len);
        const bool fw_changed = (rv != ESP_OK) || strcmp(stored_rev, DIS_FW_REV) != 0;
        if (fw_changed) {
            ESP_LOGI(TAG, "FW rev changed (\"%s\" -> \"%s\") — sending Service Changed",
                     (rv == ESP_OK) ? stored_rev : "<none>", DIS_FW_REV);
            ble_svc_gatt_changed(0x0001, 0xFFFF);
            if (nvs_set_str(fw_nvs, "fw_rev", DIS_FW_REV) == ESP_OK) {
                nvs_commit(fw_nvs);
            }
        }
        nvs_close(fw_nvs);
    } else {
        /* NVS unavailable — fall back to legacy behavior so genuine FW
         * upgrades still invalidate bonded-peer GATT caches. */
        ESP_LOGW(TAG, "NVS unavailable for FW-rev check; firing Service Changed");
        ble_svc_gatt_changed(0x0001, 0xFFFF);
    }

    ble_start_adv_internal();
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();            /* Blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void ble_fire_event(const ble_event_t *evt)
{
    if (s_ble.event_cb) {
        s_ble.event_cb(evt, s_ble.user_data);
    }
}

/* ========================================================================== */
/*  Public API                                                                */
/* ========================================================================== */

esp_err_t ble_driver_init(const ble_config_t *config)
{
    if (s_ble.initialized) {
        ESP_LOGW(TAG, "BLE already initialized");
        return ESP_OK;
    }

    /* Initialize NVS (required for NimBLE bonding) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() == ESP_OK) {
            ret = nvs_flash_init();
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Apply configuration */
    const char *name = (config && config->device_name) ?
                       config->device_name : BLE_DEVICE_NAME_DEFAULT;
    strncpy(s_ble.device_name, name, BLE_DEVICE_NAME_MAX_LEN);
    s_ble.device_name[BLE_DEVICE_NAME_MAX_LEN] = '\0';

    s_ble.appearance = config ? config->appearance : BLE_APPEARANCE_GENERIC_TAG;
    s_ble.bonding    = config ? config->enable_bonding : true;
    s_ble.mitm       = config ? config->require_mitm : false;
    s_ble.event_cb   = config ? config->event_cb : NULL;
    s_ble.user_data  = config ? config->user_data : NULL;

    /* Initialize default battery cached state per BAS 1.1.
     * Flags must match the struct layout exactly — each bit tells the peer
     * which optional field follows, so any mismatch desyncs the wire format. */
    memset(&s_ble.batt_lvl_stat_cache, 0, sizeof(s_ble.batt_lvl_stat_cache));
    s_ble.batt_lvl_stat_cache.flags = 0x06; /* bit1 Battery Level + bit2 Additional Status */
    s_ble.batt_lvl_stat_cache.power_state = 0x0001; /* bit0 Battery Present = Yes */

    memset(&s_ble.batt_energy_cache, 0, sizeof(s_ble.batt_energy_cache));
    s_ble.batt_energy_cache.flags = 0x02; /* bit1 Present Voltage */

    memset(&s_ble.batt_time_cache, 0, sizeof(s_ble.batt_time_cache));
    s_ble.batt_time_cache.flags = 0x02; /* bit1 Time Until Recharged (Time Until Discharged is mandatory) */
    memset(s_ble.batt_time_cache.time_to_discharge, 0xFF, 3); /* Unknown sentinel (0xFFFFFF) */
    memset(s_ble.batt_time_cache.time_to_recharge,  0xFF, 3);

    memset(&s_ble.batt_health_cache, 0, sizeof(s_ble.batt_health_cache));
    s_ble.batt_health_cache.flags = 0x05; /* bit0 Health Summary + bit2 Current Temperature */

    /* Initialize NimBLE */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set default high MTU to 512 */
    ble_att_set_preferred_mtu(512);

    /* Set initial TX Power to +9 dBm */
    ble_driver_set_tx_power(9);

    /* Host configuration */
    ble_hs_cfg.reset_cb          = ble_on_reset;
    ble_hs_cfg.sync_cb           = ble_on_sync;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    /* Security manager configuration */
    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;  /* Just Works default */
    ble_hs_cfg.sm_bonding        = s_ble.bonding ? 1 : 0;
    ble_hs_cfg.sm_mitm           = s_ble.mitm ? 1 : 0;
    ble_hs_cfg.sm_sc             = 1;  /* LE Secure Connections preferred */
    ble_hs_cfg.sm_oob_data_flag  = 0;  /* Will be set when OOB TK generated */
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* Initialize services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Register custom services */
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    /* Set device name */
    rc = ble_svc_gap_device_name_set(s_ble.device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "gap_device_name_set failed: %d", rc);
    }

    /* Set appearance */
    ble_svc_gap_device_appearance_set(s_ble.appearance);

    /* Configure NimBLE store for bonding persistence */
    ble_hs_cfg.store_read_cb   = ble_store_config_read;
    ble_hs_cfg.store_write_cb  = ble_store_config_write;
    ble_hs_cfg.store_delete_cb = ble_store_config_delete;

    /* Start NimBLE host task */
    nimble_port_freertos_init(ble_host_task);

    s_ble.initialized = true;

    /* Log initialization summary for production diagnostics */
    ESP_LOGI(TAG, "BLE driver initialized: \"%s\" (appearance=0x%04X, bonding=%s, SC=%s)",
             s_ble.device_name, s_ble.appearance,
             s_ble.bonding ? "on" : "off",
             "on");
    ESP_LOGI(TAG, "GATT services registered:");
    ESP_LOGI(TAG, "  [0x180A] Device Information Service");
    ESP_LOGI(TAG, "  [0x1805] Current Time Service");
    ESP_LOGI(TAG, "  [0x180F] Battery Service (5 characteristics)");
    ESP_LOGI(TAG, "  [custom] Vitals Service  (chr handle=%d)", s_ble.vitals_val_handle);
    ESP_LOGI(TAG, "  [custom] Motion Service  (raw=%d, activity=%d, mode=%d, auth=%d)",
             s_ble.raw_val_handle, s_ble.activity_val_handle,
             s_ble.mode_val_handle, s_ble.auth_ping_val_handle);
    ESP_LOGI(TAG, "  [custom] Environmental Service (chr handle=%d)", s_ble.env_val_handle);
    ESP_LOGI(TAG, "  [custom] Aggregated Service (chr handle=%d)", s_ble.agg_val_handle);
    ESP_LOGI(TAG, "  Battery handles: level=%d, stat=%d, energy=%d, time=%d, health=%d",
             s_ble.bas_chr_val_handle, s_ble.batt_lvl_stat_val_handle,
             s_ble.batt_energy_val_handle, s_ble.batt_time_val_handle,
             s_ble.batt_health_val_handle);

    /* Validate critical handles were assigned (non-zero after ble_gatts_add_svcs) */
    if (s_ble.bas_chr_val_handle == 0 || s_ble.vitals_val_handle == 0 ||
        s_ble.raw_val_handle == 0 || s_ble.env_val_handle == 0 ||
        s_ble.agg_val_handle == 0) {
        ESP_LOGE(TAG, "WARNING: One or more GATT characteristic handles are 0 — "
                 "service registration may have failed. Check NimBLE GATT table config.");
    }

    return ESP_OK;
}

esp_err_t ble_driver_deinit(void)
{
    if (!s_ble.initialized) return ESP_OK;

    if (s_ble.conn_handle != 0xFFFF) {
        ble_gap_terminate(s_ble.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
    }

    rc = nimble_port_deinit();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_deinit failed: %d", rc);
    }

    /* Restore static-init sentinels clobbered by the memset. Missing this
     * breaks BAS duplicate-suppression after a re-init (first post-init
     * SOC of 0 would match stored 0 and silently drop the notify). */
    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.conn_handle          = 0xFFFF;
    s_ble.bas_last_notified_soc = 0xFF;
    s_ble.mode_cache           = BLE_MODE_LIVE;

    ESP_LOGI(TAG, "BLE driver deinitialized");
    return ESP_OK;
}

esp_err_t ble_driver_start_advertising(void)
{
    if (!s_ble.synced) {
        ESP_LOGW(TAG, "Not synced yet, advertising will start on sync");
        return ESP_ERR_INVALID_STATE;
    }
    int rc = ble_start_adv_internal();
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_driver_stop_advertising(void)
{
    if (!s_ble.advertising) return ESP_OK;
    int rc = ble_gap_adv_stop();
    if (rc == 0) {
        s_ble.advertising = false;
    }
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

bool ble_driver_is_advertising(void)
{
    return s_ble.advertising;
}

bool ble_driver_is_connected(void)
{
    return (s_ble.conn_handle != 0xFFFF);
}

uint16_t ble_driver_get_conn_handle(void)
{
    return s_ble.conn_handle;
}

esp_err_t ble_driver_disconnect(void)
{
    if (s_ble.conn_handle == 0xFFFF) return ESP_OK;
    int rc = ble_gap_terminate(s_ble.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_driver_get_address(uint8_t *addr, uint8_t *addr_type)
{
    if (!addr) return ESP_ERR_INVALID_ARG;

    if (!s_ble.synced) {
        ESP_LOGW(TAG, "BLE not synced yet");
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(addr, s_ble.own_addr, 6);
    if (addr_type) {
        *addr_type = s_ble.own_addr_type;
    }
    return ESP_OK;
}

const char *ble_driver_get_device_name(void)
{
    return s_ble.device_name;
}

uint16_t ble_driver_get_appearance(void)
{
    return s_ble.appearance;
}

esp_err_t ble_driver_generate_oob_tk(uint8_t *tk)
{
    if (!tk) return ESP_ERR_INVALID_ARG;

    /* Generate 16 random bytes for TK */
    esp_fill_random(s_ble.oob_tk, sizeof(s_ble.oob_tk));
    s_ble.oob_tk_valid = true;

    /* Enable OOB flag in security manager */
    ble_hs_cfg.sm_oob_data_flag = 1;

    memcpy(tk, s_ble.oob_tk, 16);
    ESP_LOGI(TAG, "OOB TK generated");
    return ESP_OK;
}

esp_err_t ble_driver_clear_bonds(void)
{
    int rc = ble_store_clear();
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_store_clear failed: %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "All BLE bonds cleared");
    return ESP_OK;
}

esp_err_t ble_driver_notify_battery_level(uint8_t soc_pct)
{
    if (soc_pct > 100) soc_pct = 100;

    /* Always update the read-cache so GATT reads are correct */
    s_ble.bas_current_soc = soc_pct;

    if (s_ble.conn_handle == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;  /* No connection — nothing to notify */
    }

    /* Suppress duplicate notifications */
    if (soc_pct == s_ble.bas_last_notified_soc) {
        return ESP_OK;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&soc_pct, sizeof(soc_pct));
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* ble_gatts_notify_custom() consumes the mbuf on ALL paths (success
     * and failure) — do NOT free om after this call. */
    int rc = ble_gatts_notify_custom(s_ble.conn_handle,
                                     s_ble.bas_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGD(TAG, "BAS notify failed: %d (not subscribed?)", rc);
        return ESP_FAIL;
    }

    /* Only update tracking state if the notification actually succeeded */
    s_ble.bas_last_notified_soc = soc_pct;

    ESP_LOGD(TAG, "BAS notify: %d%%", soc_pct);
    return ESP_OK;
}


/* ========================================================================== */
/*  Telemetry Setters & Getters                                               */
/* ========================================================================== */

esp_err_t ble_driver_update_vitals(const ble_vitals_t *vitals, bool notify)
{
    if (!vitals) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.vitals_cache, vitals, sizeof(ble_vitals_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.vitals_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_raw_motion(const ble_raw_t *raw, bool notify)
{
    if (!raw) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.raw_cache, raw, sizeof(ble_raw_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.raw_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_activity(const ble_activity_t *activity, bool notify)
{
    if (!activity) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.activity_cache, activity, sizeof(ble_activity_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.activity_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_env(const ble_env_t *env, bool notify)
{
    if (!env) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.env_cache, env, sizeof(ble_env_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.env_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_aggregated(const ble_aggregated_all_t *agg, bool notify)
{
    if (!agg) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.agg_cache, agg, sizeof(ble_aggregated_all_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.agg_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_set_mode(ble_mode_t mode)
{
    if (mode > BLE_MODE_DEV) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ble.mode_cache = mode;
    return ESP_OK;
}

ble_mode_t ble_driver_get_mode(void)
{
    return s_ble.mode_cache;
}


esp_err_t ble_driver_update_batt_level_stat(const ble_batt_level_stat_t *stat, bool notify)
{
    if (!stat) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.batt_lvl_stat_cache, stat, sizeof(ble_batt_level_stat_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.batt_lvl_stat_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_batt_energy_stat(const ble_batt_energy_stat_t *stat, bool notify)
{
    if (!stat) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.batt_energy_cache, stat, sizeof(ble_batt_energy_stat_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.batt_energy_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_batt_time_stat(const ble_batt_time_stat_t *stat, bool notify)
{
    if (!stat) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.batt_time_cache, stat, sizeof(ble_batt_time_stat_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.batt_time_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_update_batt_health_stat(const ble_batt_health_stat_t *stat, bool notify)
{
    if (!stat) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.batt_health_cache, stat, sizeof(ble_batt_health_stat_t));
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.batt_health_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_sync_extended_battery(const power_snapshot_t *snap)
{
    if (!snap) return ESP_ERR_INVALID_ARG;

    /* 1. Battery Level Status (0x2BED) */
    ble_batt_level_stat_t level_stat;
    memcpy(&level_stat, &s_ble.batt_lvl_stat_cache, sizeof(ble_batt_level_stat_t));
    level_stat.battery_level = snap->soc_pct;

    uint16_t power_state = level_stat.power_state;

    /* Power State encoding per BAS 1.1 / Battery Level Status (0x2BED). */

    /* Bits 1-2 Wired External Power Source Connected: 0=No, 1=Yes, 2=Unknown, 3=RFU */
    uint8_t wired_ext = snap->usb_connected ? 1 /* Yes */ : 0 /* No */;
    power_state &= ~(0x03u << 1);
    power_state |= (wired_ext & 0x03u) << 1;

    /* Bits 5-6 Battery Charge State: 0=Unknown, 1=Charging, 2=Discharging:Active, 3=Discharging:Inactive.
     * BQ25896 ChrgStat: 0=Not Charging, 1=Pre-charge, 2=Fast charge, 3=Charge Done. */
    uint8_t charge_state;
    if (!snap->usb_connected) {
        charge_state = 2; /* Discharging: Active */
    } else if (snap->charge_status == 1 || snap->charge_status == 2) {
        charge_state = 1; /* Charging */
    } else if (snap->charge_status == 3) {
        charge_state = 3; /* Discharging: Inactive — battery full, external power supplying load */
    } else {
        charge_state = 0; /* Unknown */
    }
    power_state &= ~(0x03u << 5);
    power_state |= (charge_state & 0x03u) << 5;

    /* Bits 7-8 Battery Charge Level: 0=Unknown, 1=Good, 2=Low, 3=Critical */
    uint8_t charge_level = (snap->soc_pct <= BMS_CRITICAL_BATTERY_PCT) ? 3 /* Critical */
                                                                      : 1 /* Good */;
    power_state &= ~(0x03u << 7);
    power_state |= (charge_level & 0x03u) << 7;

    level_stat.power_state = power_state;
    ble_driver_update_batt_level_stat(&level_stat, true);

    ESP_LOGI(TAG, "BAS sync: soc=%u%% v=%umV temp=%.1fC usb=%d chrg=%u tte=%u ttf=%u conn=%s",
             snap->soc_pct, snap->voltage_mv, snap->temperature_c,
             (int)snap->usb_connected, snap->charge_status,
             snap->tte_min, snap->ttf_min,
             (s_ble.conn_handle != 0xFFFF) ? "yes" : "no");

    /* 2. Battery Energy Status (0x2BF0) — Present Voltage as IEEE-11073 SFLOAT in volts */
    ble_batt_energy_stat_t energy_stat;
    memcpy(&energy_stat, &s_ble.batt_energy_cache, sizeof(ble_batt_energy_stat_t));
    energy_stat.curr_voltage = medfloat16_volts_from_mv(snap->voltage_mv);
    ble_driver_update_batt_energy_stat(&energy_stat, true);

    /* 3. Battery Time Status (0x2BEE) */
    ble_batt_time_stat_t time_stat;
    memcpy(&time_stat, &s_ble.batt_time_cache, sizeof(ble_batt_time_stat_t));
    if (snap->tte_min != 0xFFFF) {
        time_stat.time_to_discharge[0] = snap->tte_min & 0xFF;
        time_stat.time_to_discharge[1] = (snap->tte_min >> 8) & 0xFF;
        time_stat.time_to_discharge[2] = 0;
    } else {
        memset(time_stat.time_to_discharge, 0xFF, 3);
    }
    if (snap->ttf_min != 0xFFFF) {
        time_stat.time_to_recharge[0] = snap->ttf_min & 0xFF;
        time_stat.time_to_recharge[1] = (snap->ttf_min >> 8) & 0xFF;
        time_stat.time_to_recharge[2] = 0;
    } else {
        memset(time_stat.time_to_recharge, 0xFF, 3);
    }
    ble_driver_update_batt_time_stat(&time_stat, true);

    /* 4. Battery Health Status (0x2BEA) — clamp temperature to spec sentinels to
     * avoid UB from float→int8 cast when out of range. */
    ble_batt_health_stat_t health_stat;
    memcpy(&health_stat, &s_ble.batt_health_cache, sizeof(ble_batt_health_stat_t));
    const float t = snap->temperature_c;
    if (t > 126.0f) {
        health_stat.current_temp = (int8_t)0x7F; /* ">126 °C" */
    } else if (t < -127.0f) {
        health_stat.current_temp = (int8_t)0x80; /* "<-127 °C" */
    } else {
        health_stat.current_temp = (int8_t)(t >= 0 ? t + 0.5f : t - 0.5f);
    }
    ble_driver_update_batt_health_stat(&health_stat, true);

    return ESP_OK;
}

void ble_driver_batt_update_power_state(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault) {
    if(wired_ext > 2) return;
    if(charge_state > 3) return;
    if(charge_level > 3) return;
    if(charge_type > 4) return;
    if(charge_fault > 7) return;

    uint16_t newPowerState = s_ble.batt_lvl_stat_cache.power_state;

    /* Clear relevant bits and set new values matching the C++ specification exactly */
    newPowerState &= ~(0x03 << 1);
    newPowerState |= (wired_ext & 0x03) << 1; 

    newPowerState &= ~(0x03 << 5);
    newPowerState |= (charge_state & 0x03) << 5; 

    newPowerState &= ~(0x03 << 7);
    newPowerState |= (charge_level & 0x03) << 7; 

    newPowerState &= ~(0x07 << 9);
    newPowerState |= (charge_type & 0x07) << 9; 

    newPowerState &= ~(0x07 << 12);
    newPowerState |= (charge_fault & 0x07) << 12; 

    s_ble.batt_lvl_stat_cache.power_state = newPowerState;
}

void ble_driver_batt_update_level(uint8_t battery_level) {
    if (battery_level > 100) return;
    s_ble.batt_lvl_stat_cache.battery_level = battery_level;
}

void ble_driver_batt_update_additional_status(uint8_t service_req, uint8_t batt_fault) {
    /* Additional Status (0x2BED): bits 0-1 Service Required (0=False,1=True,2=Unknown,3=RFU),
     * bit 2 Battery Fault (0=False/Unknown, 1=Yes), bits 3-7 RFU. */
    if (service_req > 2) return;
    if (batt_fault > 1) return;

    uint8_t v = s_ble.batt_lvl_stat_cache.additional_status;
    v &= ~(uint8_t)0x03;            /* bits 0-1 */
    v |= (service_req & 0x03u);
    v &= ~(uint8_t)(0x01u << 2);    /* bit 2 only — Battery Fault is 1 bit per spec */
    v |= (uint8_t)((batt_fault & 0x01u) << 2);
    s_ble.batt_lvl_stat_cache.additional_status = v;
}

void ble_driver_batt_update_curr_voltage(uint16_t mv) {
    /* Accepts millivolts; encodes to IEEE-11073 SFLOAT (volts) for on-wire storage. */
    s_ble.batt_energy_cache.curr_voltage = medfloat16_volts_from_mv(mv);
}

void ble_driver_batt_update_time_discharge(const uint8_t time_to_discharge[3]) {
    memcpy(s_ble.batt_time_cache.time_to_discharge, time_to_discharge, 3);
}

void ble_driver_batt_update_time_recharge(const uint8_t time_to_recharge[3]) {
    memcpy(s_ble.batt_time_cache.time_to_recharge, time_to_recharge, 3);
}

void ble_driver_batt_update_health_summary(uint8_t health_summary) {
    s_ble.batt_health_cache.health_summary = health_summary;
}

void ble_driver_batt_update_current_temp(int8_t current_temp) {
    s_ble.batt_health_cache.current_temp = current_temp;
}

/* ========================================================================== */
/*  Connection & Metric Polling                                               */
/* ========================================================================== */

bool ble_driver_is_authenticated(void) {
    if (s_ble.conn_handle == 0xFFFF) return false;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(s_ble.conn_handle, &desc) == 0) {
        return (desc.sec_state.bonded || desc.sec_state.encrypted);
    }
    return false;
}

int8_t ble_driver_get_rssi(void) {
    if (s_ble.conn_handle == 0xFFFF) return -127;
    int8_t rssi = -127;
    int rc = ble_gap_conn_rssi(s_ble.conn_handle, &rssi);
    if (rc != 0) return -127;
    return rssi;
}

void ble_driver_set_tx_power(int8_t tx_power) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    int esp_pwr = (tx_power / 3) + 4; // Map dbm to esp_power_level_t roughly (-12 to +9 max)
    if (esp_pwr < 0) esp_pwr = 0;
    if (esp_pwr > 7) esp_pwr = 7;

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)esp_pwr);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, (esp_power_level_t)esp_pwr);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, (esp_power_level_t)esp_pwr);
#endif
}

/* ========================================================================== */
/*  Piecemeal Component Setters                                               */
/* ========================================================================== */
esp_err_t ble_driver_set_accel(const ble_accel_t *accel) {
    if (!accel) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.raw_cache.accel, accel, sizeof(ble_accel_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_gyro(const ble_gyro_t *gyro) {
    if (!gyro) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.raw_cache.gyro, gyro, sizeof(ble_gyro_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_magf(const ble_magf_t *magf) {
    if (!magf) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.raw_cache.magf, magf, sizeof(ble_magf_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_rv(const ble_rv_t *rv) {
    if (!rv) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.raw_cache.rv, rv, sizeof(ble_rv_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_step_count(const ble_step_count_t *step) {
    if (!step) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.activity_cache.stepCount, step, sizeof(ble_step_count_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_activity_class(const ble_activity_class_t *act_class) {
    if (!act_class) return ESP_ERR_INVALID_ARG;
    memcpy(&s_ble.activity_cache.activityClass, act_class, sizeof(ble_activity_class_t));
    return ESP_OK;
}

esp_err_t ble_driver_set_hr(uint8_t hr, uint8_t hr_acc) {
    s_ble.vitals_cache.heart_rate = hr;
    s_ble.vitals_cache.hr_confidence = hr_acc;
    return ESP_OK;
}

esp_err_t ble_driver_set_br(uint8_t br) {
    s_ble.vitals_cache.breath_rate = br;
    return ESP_OK;
}

esp_err_t ble_driver_set_temp(float temp) {
    s_ble.env_cache.temperature = temp;
    return ESP_OK;
}

esp_err_t ble_driver_set_humidity(float hum) {
    s_ble.env_cache.humidity = hum;
    return ESP_OK;
}

/* ========================================================================== */
/*  Auto-Timestamp Dispatchers                                                */
/* ========================================================================== */

esp_err_t ble_driver_notify_vitals(bool notify) {
    rtc_get_datetime_utc(&s_ble.vitals_cache.timestamp);
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.vitals_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_notify_raw(bool notify) {
    rtc_get_datetime_utc(&s_ble.raw_cache.timestamp);
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.raw_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_notify_activity(bool notify) {
    rtc_get_datetime_utc(&s_ble.activity_cache.timestamp);
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.activity_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_notify_env(bool notify) {
    rtc_get_datetime_utc(&s_ble.env_cache.timestamp);
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.env_val_handle);
    }
    return ESP_OK;
}

esp_err_t ble_driver_notify_aggregated(bool notify) {
    // Aggregated didn't have top level timestamp, just nested
    // We will just dispatch since the aggregated getters should handle nested population
    if (notify && s_ble.conn_handle != 0xFFFF) {
        ble_gatts_chr_updated(s_ble.agg_val_handle);
    }
    return ESP_OK;
}