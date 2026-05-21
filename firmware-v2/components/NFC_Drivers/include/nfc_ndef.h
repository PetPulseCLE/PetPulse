/**
 * @file nfc_ndef.h
 * @brief NDEF (NFC Data Exchange Format) message and record builder.
 *
 * Provides utilities for constructing NDEF messages, with specific helpers
 * for BLE Connection Handover records (NFC Forum Connection Handover v1.5).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  NDEF TNF (Type Name Format) codes                                         */
/* ========================================================================== */

#define NDEF_TNF_EMPTY          0x00
#define NDEF_TNF_WELL_KNOWN     0x01
#define NDEF_TNF_MEDIA_TYPE     0x02
#define NDEF_TNF_ABSOLUTE_URI   0x03
#define NDEF_TNF_EXTERNAL       0x04
#define NDEF_TNF_UNKNOWN        0x05
#define NDEF_TNF_UNCHANGED      0x06

/* ========================================================================== */
/*  NDEF Record Flags                                                         */
/* ========================================================================== */

#define NDEF_FLAG_MB    (1U << 7)   /**< Message Begin */
#define NDEF_FLAG_ME    (1U << 6)   /**< Message End */
#define NDEF_FLAG_CF    (1U << 5)   /**< Chunk Flag */
#define NDEF_FLAG_SR    (1U << 4)   /**< Short Record (payload ≤ 255) */
#define NDEF_FLAG_IL    (1U << 3)   /**< ID Length present */

/* ========================================================================== */
/*  BLE OOB AD (Advertising Data) types                                       */
/* ========================================================================== */

#define BLE_AD_TYPE_FLAGS                   0x01
#define BLE_AD_TYPE_COMPLETE_LOCAL_NAME     0x09
#define BLE_AD_TYPE_TK_VALUE               0x10
#define BLE_AD_TYPE_APPEARANCE             0x19
#define BLE_AD_TYPE_LE_BD_ADDR             0x1B
#define BLE_AD_TYPE_LE_ROLE                0x1C
#define BLE_AD_TYPE_LE_SC_CONFIRM          0x22
#define BLE_AD_TYPE_LE_SC_RANDOM           0x23

/** LE Roles */
#define BLE_LE_ROLE_PERIPH_ONLY            0x00
#define BLE_LE_ROLE_CENTRAL_ONLY           0x01
#define BLE_LE_ROLE_PERIPH_PREFERRED       0x02
#define BLE_LE_ROLE_CENTRAL_PREFERRED      0x03

/* ========================================================================== */
/*  NDEF Record                                                               */
/* ========================================================================== */

/**
 * @brief Single NDEF record (in-memory representation).
 * The record is serialized into a flat buffer by ndef_record_serialize().
 */
typedef struct {
    uint8_t  tnf;               /**< Type Name Format (3 bits) */
    const uint8_t *type;        /**< Type string/bytes (not null-terminated) */
    uint8_t  type_len;          /**< Length of type */
    const uint8_t *id;          /**< Record ID (optional, may be NULL) */
    uint8_t  id_len;            /**< Length of ID */
    const uint8_t *payload;     /**< Payload data */
    uint32_t payload_len;       /**< Payload length */
} ndef_record_t;

/* ========================================================================== */
/*  NDEF Message Buffer                                                       */
/* ========================================================================== */

/** Maximum NDEF message size for Type 4 Tag (including 2-byte NLEN) */
#define NDEF_MSG_MAX_SIZE  512

/**
 * @brief Pre-allocated NDEF message buffer.
 */
typedef struct {
    uint8_t  data[NDEF_MSG_MAX_SIZE];
    size_t   len;       /**< Current bytes written */
    int      count;     /**< Number of records added */
} ndef_message_t;

/* ========================================================================== */
/*  Record Building API                                                       */
/* ========================================================================== */

/**
 * @brief Initialize an NDEF message buffer (clears to empty).
 */
void ndef_message_init(ndef_message_t *msg);

/**
 * @brief Append a record to an NDEF message.
 * Automatically sets MB/ME flags based on position.
 * @return Number of bytes written, or 0 on overflow.
 */
size_t ndef_message_append_record(ndef_message_t *msg, const ndef_record_t *rec);

/**
 * @brief Finalize the message (ensures ME flag is set on last record).
 */
void ndef_message_finalize(ndef_message_t *msg);

/**
 * @brief Serialize a single NDEF record into a flat buffer.
 * @param rec     Record to serialize
 * @param buf     Output buffer
 * @param buf_len Buffer capacity
 * @param is_mb   Message Begin flag
 * @param is_me   Message End flag
 * @return Number of bytes written, or 0 on overflow.
 */
size_t ndef_record_serialize(const ndef_record_t *rec, uint8_t *buf, size_t buf_len,
                             bool is_mb, bool is_me);

/* ========================================================================== */
/*  BLE Handover Helpers                                                      */
/* ========================================================================== */

/**
 * @brief Build a complete NDEF Handover Select message for BLE tap-to-pair.
 *
 * The message contains:
 *  1. Handover Select record (type "Hs", v1.5, with Alternative Carrier)
 *  2. BLE LE OOB carrier configuration record
 *
 * Compatible with both iPhone (iOS 14+) and Android (4.0+).
 *
 * @param msg           Output NDEF message buffer
 * @param ble_addr      6-byte BLE device address (little-endian)
 * @param ble_addr_type 0 = public, 1 = random
 * @param device_name   Device name (null-terminated string)
 * @param appearance    BLE appearance value (2 bytes, little-endian)
 * @param tk            Optional 16-byte TK for OOB pairing (NULL to omit)
 * @return              Total NDEF message length, or 0 on error
 */
size_t ndef_build_ble_handover_select(
    ndef_message_t *msg,
    const uint8_t *ble_addr,
    uint8_t ble_addr_type,
    const char *device_name,
    uint16_t appearance,
    const uint8_t *tk);

/**
 * @brief Build the BLE LE OOB payload (sequence of AD structures).
 * @param buf           Output buffer
 * @param buf_len       Buffer capacity
 * @param ble_addr      6-byte BLE address
 * @param ble_addr_type Address type
 * @param device_name   Device name
 * @param appearance    Appearance value
 * @param tk            Optional 16-byte TK (NULL to omit)
 * @return              Number of bytes written, or 0 on error
 */
size_t ndef_build_ble_oob_payload(
    uint8_t *buf, size_t buf_len,
    const uint8_t *ble_addr,
    uint8_t ble_addr_type,
    const char *device_name,
    uint16_t appearance,
    const uint8_t *tk);

#ifdef __cplusplus
}
#endif
