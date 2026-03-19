/**
 * @file nfc_ndef.c
 * @brief NDEF record/message builder and BLE Handover Select constructor.
 *
 * Implements NFC Forum NDEF specification and Connection Handover v1.5
 * for BLE tap-to-pair with iPhone and Android.
 */

#include "nfc_ndef.h"
#include <string.h>
#include "esp_log.h"

#define TAG "NDEF"

/* ========================================================================== */
/*  Well-known type strings                                                   */
/* ========================================================================== */

static const uint8_t ndef_type_hs[] = { 'H', 's' };       /* Handover Select */
static const uint8_t ndef_type_ac[] = { 'a', 'c' };       /* Alternative Carrier */
static const char    ndef_mime_ble_oob[] = "application/vnd.bluetooth.le.oob";

/** Carrier data reference ID used to link Hs → OOB record */
static const uint8_t carrier_ref_id[] = { '0' };

/* ========================================================================== */
/*  Record Serialization                                                      */
/* ========================================================================== */

size_t ndef_record_serialize(const ndef_record_t *rec, uint8_t *buf, size_t buf_len,
                             bool is_mb, bool is_me)
{
    if (!rec || !buf) return 0;

    bool short_record = (rec->payload_len <= 255);
    bool has_id       = (rec->id != NULL && rec->id_len > 0);

    /* Calculate total size */
    size_t header_size = 1 /* flags */
                       + 1 /* type length */
                       + (short_record ? 1 : 4) /* payload length */
                       + (has_id ? 1 : 0) /* id length */
                       + rec->type_len
                       + (has_id ? rec->id_len : 0);
    size_t total = header_size + rec->payload_len;

    if (total > buf_len) return 0;

    size_t pos = 0;

    /* Flags byte */
    uint8_t flags = (rec->tnf & 0x07);
    if (is_mb)        flags |= NDEF_FLAG_MB;
    if (is_me)        flags |= NDEF_FLAG_ME;
    if (short_record) flags |= NDEF_FLAG_SR;
    if (has_id)       flags |= NDEF_FLAG_IL;
    buf[pos++] = flags;

    /* Type length */
    buf[pos++] = rec->type_len;

    /* Payload length */
    if (short_record) {
        buf[pos++] = (uint8_t)rec->payload_len;
    } else {
        buf[pos++] = (uint8_t)((rec->payload_len >> 24) & 0xFF);
        buf[pos++] = (uint8_t)((rec->payload_len >> 16) & 0xFF);
        buf[pos++] = (uint8_t)((rec->payload_len >>  8) & 0xFF);
        buf[pos++] = (uint8_t)((rec->payload_len >>  0) & 0xFF);
    }

    /* ID length */
    if (has_id) {
        buf[pos++] = rec->id_len;
    }

    /* Type */
    if (rec->type_len > 0 && rec->type) {
        memcpy(&buf[pos], rec->type, rec->type_len);
        pos += rec->type_len;
    }

    /* ID */
    if (has_id) {
        memcpy(&buf[pos], rec->id, rec->id_len);
        pos += rec->id_len;
    }

    /* Payload */
    if (rec->payload_len > 0 && rec->payload) {
        memcpy(&buf[pos], rec->payload, rec->payload_len);
        pos += rec->payload_len;
    }

    return pos;
}

/* ========================================================================== */
/*  Message Building                                                          */
/* ========================================================================== */

void ndef_message_init(ndef_message_t *msg)
{
    if (!msg) return;
    memset(msg, 0, sizeof(*msg));
}

size_t ndef_message_append_record(ndef_message_t *msg, const ndef_record_t *rec)
{
    if (!msg || !rec) return 0;

    bool is_mb = (msg->count == 0);
    bool is_me = false; /* Caller should call finalize() or this is set later */

    size_t remaining = NDEF_MSG_MAX_SIZE - msg->len;
    size_t written = ndef_record_serialize(rec, &msg->data[msg->len], remaining,
                                           is_mb, is_me);
    if (written == 0) {
        ESP_LOGE(TAG, "NDEF message overflow");
        return 0;
    }

    msg->len += written;
    msg->count++;
    return written;
}

void ndef_message_finalize(ndef_message_t *msg)
{
    if (!msg || msg->len == 0) return;

    /* Walk backwards to find the last record's flags byte and set ME */
    /* The flag byte of the last record can be found by re-parsing,
     * but since we append sequentially, the last record starts at
     * the position we can track.  For simplicity, scan for the last
     * record header. */

    /* Simple approach: walk forward through all records */
    size_t pos = 0;
    size_t last_flags_pos = 0;

    while (pos < msg->len) {
        last_flags_pos = pos;
        uint8_t flags = msg->data[pos++];
        if (pos >= msg->len) break;

        uint8_t type_len = msg->data[pos++];
        if (pos >= msg->len) break;

        uint32_t payload_len;
        if (flags & NDEF_FLAG_SR) {
            payload_len = msg->data[pos++];
        } else {
            if (pos + 4 > msg->len) break;
            payload_len = ((uint32_t)msg->data[pos] << 24) |
                          ((uint32_t)msg->data[pos+1] << 16) |
                          ((uint32_t)msg->data[pos+2] << 8) |
                          ((uint32_t)msg->data[pos+3]);
            pos += 4;
        }

        uint8_t id_len = 0;
        if (flags & NDEF_FLAG_IL) {
            if (pos >= msg->len) break;
            id_len = msg->data[pos++];
        }

        pos += type_len + id_len + payload_len;

        if (flags & NDEF_FLAG_ME) {
            /* Already finalized */
            return;
        }
    }

    /* Set ME on last record */
    msg->data[last_flags_pos] |= NDEF_FLAG_ME;
}

/* ========================================================================== */
/*  BLE OOB Payload Builder                                                   */
/* ========================================================================== */

size_t ndef_build_ble_oob_payload(
    uint8_t *buf, size_t buf_len,
    const uint8_t *ble_addr,
    uint8_t ble_addr_type,
    const char *device_name,
    uint16_t appearance,
    const uint8_t *tk)
{
    if (!buf || !ble_addr || !device_name) return 0;

    size_t pos = 0;
    size_t name_len = strlen(device_name);

    /* AD: LE Bluetooth Device Address — len=8, type=0x1B, addr[6], type[1] */
    if (pos + 9 > buf_len) return 0;
    buf[pos++] = 8;                          /* length (type + data) */
    buf[pos++] = BLE_AD_TYPE_LE_BD_ADDR;
    memcpy(&buf[pos], ble_addr, 6);          /* Address (little-endian) */
    pos += 6;
    buf[pos++] = ble_addr_type;              /* 0=public, 1=random */

    /* AD: LE Role — len=2, type=0x1C, role=0x00 (peripheral only) */
    if (pos + 3 > buf_len) return 0;
    buf[pos++] = 2;
    buf[pos++] = BLE_AD_TYPE_LE_ROLE;
    buf[pos++] = BLE_LE_ROLE_PERIPH_ONLY;

    /* AD: Flags — len=2, type=0x01, flags=0x06 (LE General Disc + BR/EDR Not Supported) */
    if (pos + 3 > buf_len) return 0;
    buf[pos++] = 2;
    buf[pos++] = BLE_AD_TYPE_FLAGS;
    buf[pos++] = 0x06;

    /* AD: Complete Local Name — len=1+name_len, type=0x09, name[] */
    if (pos + 2 + name_len > buf_len) return 0;
    buf[pos++] = (uint8_t)(1 + name_len);
    buf[pos++] = BLE_AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&buf[pos], device_name, name_len);
    pos += name_len;

    /* AD: Appearance — len=3, type=0x19, appearance[2] LE */
    if (pos + 4 > buf_len) return 0;
    buf[pos++] = 3;
    buf[pos++] = BLE_AD_TYPE_APPEARANCE;
    buf[pos++] = (uint8_t)(appearance & 0xFF);
    buf[pos++] = (uint8_t)((appearance >> 8) & 0xFF);

    /* AD: Security Manager TK Value (optional, for OOB pairing) */
    if (tk != NULL) {
        if (pos + 18 > buf_len) return 0;
        buf[pos++] = 17;                    /* length = 1 + 16 */
        buf[pos++] = BLE_AD_TYPE_TK_VALUE;
        memcpy(&buf[pos], tk, 16);
        pos += 16;
    }

    return pos;
}

/* ========================================================================== */
/*  BLE Handover Select Message Builder                                       */
/* ========================================================================== */

size_t ndef_build_ble_handover_select(
    ndef_message_t *msg,
    const uint8_t *ble_addr,
    uint8_t ble_addr_type,
    const char *device_name,
    uint16_t appearance,
    const uint8_t *tk)
{
    if (!msg || !ble_addr || !device_name) return 0;

    ndef_message_init(msg);

    /* ================================================================== */
    /* Step 1: Build the embedded Alternative Carrier record              */
    /* ================================================================== */

    /* AC payload: CPS(1) + carrier_ref_len(1) + carrier_ref(1) + aux_count(1) */
    uint8_t ac_payload[] = {
        0x01,                       /* CPS = Active */
        0x01,                       /* Carrier data reference length = 1 */
        '0',                        /* Carrier data reference = "0" */
        0x00,                       /* Auxiliary data reference count = 0 */
    };

    /* Serialize the AC record as an embedded NDEF message inside Hs payload */
    ndef_record_t ac_rec = {
        .tnf         = NDEF_TNF_WELL_KNOWN,
        .type        = ndef_type_ac,
        .type_len    = sizeof(ndef_type_ac),
        .id          = NULL,
        .id_len      = 0,
        .payload     = ac_payload,
        .payload_len = sizeof(ac_payload),
    };

    uint8_t ac_buf[32];
    size_t ac_len = ndef_record_serialize(&ac_rec, ac_buf, sizeof(ac_buf),
                                          true, true); /* MB=1, ME=1 */
    if (ac_len == 0) return 0;

    /* ================================================================== */
    /* Step 2: Build the Handover Select payload: version + AC record     */
    /* ================================================================== */

    uint8_t hs_payload[64];
    hs_payload[0] = 0x15;   /* Connection Handover version 1.5 */
    memcpy(&hs_payload[1], ac_buf, ac_len);
    size_t hs_payload_len = 1 + ac_len;

    /* ================================================================== */
    /* Step 3: Append the Handover Select record to the message           */
    /* ================================================================== */

    ndef_record_t hs_rec = {
        .tnf         = NDEF_TNF_WELL_KNOWN,
        .type        = ndef_type_hs,
        .type_len    = sizeof(ndef_type_hs),
        .id          = NULL,
        .id_len      = 0,
        .payload     = hs_payload,
        .payload_len = hs_payload_len,
    };

    if (ndef_message_append_record(msg, &hs_rec) == 0) return 0;

    /* ================================================================== */
    /* Step 4: Build BLE LE OOB payload (AD structures)                   */
    /* ================================================================== */

    uint8_t oob_payload[128];
    size_t oob_len = ndef_build_ble_oob_payload(
        oob_payload, sizeof(oob_payload),
        ble_addr, ble_addr_type, device_name, appearance, tk);
    if (oob_len == 0) return 0;

    /* ================================================================== */
    /* Step 5: Append BLE OOB carrier config record with ID="0"           */
    /* ================================================================== */

    ndef_record_t oob_rec = {
        .tnf         = NDEF_TNF_MEDIA_TYPE,
        .type        = (const uint8_t *)ndef_mime_ble_oob,
        .type_len    = (uint8_t)strlen(ndef_mime_ble_oob),
        .id          = carrier_ref_id,
        .id_len      = sizeof(carrier_ref_id),
        .payload     = oob_payload,
        .payload_len = oob_len,
    };

    if (ndef_message_append_record(msg, &oob_rec) == 0) return 0;

    /* ================================================================== */
    /* Step 6: Finalize (set ME on last record)                           */
    /* ================================================================== */

    ndef_message_finalize(msg);

    ESP_LOGI(TAG, "BLE Handover Select NDEF built: %u bytes, %d records",
             (unsigned)msg->len, msg->count);

    return msg->len;
}
