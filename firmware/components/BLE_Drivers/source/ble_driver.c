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
#include "bms_driver.h"

#include <string.h>
#include <assert.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"

/* NimBLE includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

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
    uint8_t             bas_last_soc;        /**< Last notified SOC to suppress duplicates */
} s_ble = {
    .conn_handle = 0xFFFF,
};

/* Forward declarations */
static int  ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_host_task(void *param);
static void ble_fire_event(const ble_event_t *evt);

/* ========================================================================== */
/*  GATT Services                                                             */
/* ========================================================================== */

/*
 * Device Information Service (0x180A) — read-only characteristics:
 *   - Manufacturer Name   (0x2A29)
 *   - Model Number         (0x2A24)
 *   - Firmware Revision    (0x2A26)
 *   - System ID            (0x2A23)
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
        return (err == ESP_OK) ? 0 : BLE_ATT_ERR_UNLIKELY;
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

    /* Read cached SOC — zero I2C overhead */
    power_snapshot_t snap;
    uint8_t level = 0;
    if (power_manager_get_snapshot(&snap)) {
        level = snap.soc_pct;
        if (level > 100) level = 100;  /* Clamp per BAS spec */
    }

    int rc = os_mbuf_append(ctxt->om, &level, sizeof(level));
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
                         BLE_GATT_CHR_F_WRITE,
            },
            { 0 }, /* Sentinel */
        },
    },
    {
        /* Battery Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Battery Level */
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = gatt_bas_access,
                .val_handle = &s_ble.bas_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, /* Sentinel */
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

    /* --- Primary advertising data --- */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* Shortened name in ADV, full name in scan response */
    size_t name_len = strlen(s_ble.device_name);
    if (name_len <= 10) {
        fields.name = (uint8_t *)s_ble.device_name;
        fields.name_len = name_len;
        fields.name_is_complete = 1;
    } else {
        /* Put short name in ADV, full in scan response */
        fields.name = (uint8_t *)s_ble.device_name;
        fields.name_len = 10;
        fields.name_is_complete = 0;
    }

    /* Appearance */
    fields.appearance = s_ble.appearance;
    fields.appearance_is_present = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return rc;
    }

    /* --- Scan response data (full name + service UUIDs) --- */
    rsp_fields.name = (uint8_t *)s_ble.device_name;
    rsp_fields.name_len = name_len;
    rsp_fields.name_is_complete = 1;

    /* Advertise DIS (0x180A) + CTS (0x1805) + BAS (0x180F) UUIDs */
    static ble_uuid16_t svc_uuids[] = {
        BLE_UUID16_INIT(0x180A),
        BLE_UUID16_INIT(0x1805),
        BLE_UUID16_INIT(0x180F),
    };
    rsp_fields.uuids16 = svc_uuids;
    rsp_fields.num_uuids16 = 3;
    rsp_fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return rc;
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

            /* Request MTU exchange for best throughput */
            ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);

            /* Request connection parameter update for iPhone compatibility:
             * interval 15-30 ms, latency 0, timeout 4 s */
            struct ble_gap_upd_params params = {
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,  /* 7.5 ms */
                .itvl_max = 24,                               /* 30 ms */
                .latency  = 0,
                .supervision_timeout = 400,                   /* 4 s */
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            ble_gap_update_params(event->connect.conn_handle, &params);

            /* Initiate security (pairing) */
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "security_initiate failed: %d", rc);
            }

            ble_gap_conn_find(event->connect.conn_handle, &desc);
            ble_event_t evt = {
                .type = BLE_EVENT_CONNECTED,
                .conn_handle = event->connect.conn_handle,
            };
            memcpy(evt.connected.peer_addr, desc.peer_id_addr.val, 6);
            evt.connected.addr_type = desc.peer_id_addr.type;
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

        {
            ble_event_t evt = {
                .type = BLE_EVENT_DISCONNECTED,
                .conn_handle = event->disconnect.conn.conn_handle,
            };
            evt.disconnected.reason = event->disconnect.reason;
            ble_fire_event(&evt);
        }

        /* Auto-restart advertising */
        ble_start_adv_internal();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d",
                 event->enc_change.status);
        if (event->enc_change.status == 0) {
            ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            ble_event_t evt = {
                .type = BLE_EVENT_PAIRED,
                .conn_handle = event->enc_change.conn_handle,
            };
            evt.paired.bonded = desc.sec_state.bonded;
            evt.paired.encrypted = desc.sec_state.encrypted;
            ble_fire_event(&evt);
        } else {
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
        ESP_LOGD(TAG, "Subscribe event: cur_notify=%d",
                 event->subscribe.cur_notify);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGD(TAG, "Connection updated: status=%d",
                 event->conn_update.status);
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
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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

    /* Initialize NimBLE */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

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
    ESP_LOGI(TAG, "BLE driver initialized: \"%s\"", s_ble.device_name);

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

    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.conn_handle = 0xFFFF;

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

    /* Suppress duplicate notifications */
    if (soc_pct == s_ble.bas_last_soc) {
        return ESP_OK;
    }
    s_ble.bas_last_soc = soc_pct;

    if (s_ble.conn_handle == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;  /* No connection — nothing to notify */
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&soc_pct, sizeof(soc_pct));
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_ble.conn_handle,
                                     s_ble.bas_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGD(TAG, "BAS notify failed: %d (not subscribed?)", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "BAS notify: %d%%", soc_pct);
    return ESP_OK;
}
