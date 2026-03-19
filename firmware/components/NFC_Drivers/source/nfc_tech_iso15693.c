/**
 * @file nfc_tech_iso15693.c
 * @brief ISO15693 (NFC-V) technology implementation (ported from Flipper Zero).
 *
 * The poller implements full 1-out-of-4 frame encoding/decoding in software
 * because the ST25R3916 operates in subcarrier stream mode for this technology.
 *
 * The listener is stubbed — Flipper's ISO15693 listener relies on transparent
 * mode with GPIO bit-banging (iso15693_signal / iso15693_parser) that cannot
 * be directly ported.
 */

#include "nfc_driver.h"
#include "st25r3916_driver.h"
#include <string.h>
#include "esp_log.h"

#define TAG "NfcIso15693"

/** FWT compensation for ISO15693 poller */
#define NFC_ISO15693_POLLER_FWT_COMP_FC (-1300)
/** FDT for ISO15693 listener */
#define NFC_ISO15693_LISTENER_FDT_FC    (2850)

/* ========================================================================== */
/*  Frame Encoding / Decoding (1-out-of-4, 424 kHz subcarrier)                */
/* ========================================================================== */

/* SOF/EOF patterns */
#define ISO15693_SOF_PATTERN  (0x21)
#define ISO15693_SOF_BITS     (8)
#define ISO15693_EOF_PATTERN  (0x04)
#define ISO15693_EOF_BITS     (4)

/* Decoding SOF/EOF patterns */
#define ISO15693_RX_SOF_PATTERN  (0x17)
#define ISO15693_RX_SOF_BITS     (5)
#define ISO15693_RX_EOF_PATTERN  (0x1D)
#define ISO15693_RX_EOF_BITS     (5)

/** 2-bit encoding table for 1-out-of-4 */
static const uint8_t iso15693_bit_patterns[4] = {
    0x02, /* 00 */
    0x08, /* 01 */
    0x20, /* 10 */
    0x80, /* 11 */
};

/** FIFO buffer for raw subcarrier data */
static uint8_t iso15693_fifo_buf[256];

/**
 * @brief Encode an ISO15693 frame into 1-out-of-4 subcarrier pulses.
 *
 * Produces: SOF + data encoded 2 bits at a time + EOF.
 *
 * @param tx_data     Raw data bytes to encode
 * @param tx_bytes    Number of bytes to encode
 * @param encoded_buf Output buffer (must be at least 4 + tx_bytes*4 + 1 bytes)
 * @return            Number of bits in the encoded buffer
 */
static size_t nfc_iso15693_encode_frame(
    const uint8_t* tx_data,
    size_t tx_bytes,
    uint8_t* encoded_buf) {

    size_t bit_pos = 0;
    size_t byte_pos = 0;
    uint8_t bit_accum = 0;
    int bit_count = 0;

#define EMIT_BITS(pattern, nbits) do { \
    uint32_t _p = (pattern); \
    int _n = (nbits); \
    for (int _i = _n - 1; _i >= 0; _i--) { \
        bit_accum = (bit_accum << 1) | ((_p >> _i) & 1); \
        bit_count++; \
        if (bit_count == 8) { \
            encoded_buf[byte_pos++] = bit_accum; \
            bit_accum = 0; \
            bit_count = 0; \
        } \
        bit_pos++; \
    } \
} while(0)

    /* SOF */
    EMIT_BITS(ISO15693_SOF_PATTERN, ISO15693_SOF_BITS);

    /* Data: each byte encoded as 4 × 2-bit symbols */
    for (size_t i = 0; i < tx_bytes; i++) {
        uint8_t byte = tx_data[i];
        for (int nibble = 0; nibble < 4; nibble++) {
            uint8_t symbol = byte & 0x03;
            byte >>= 2;
            uint8_t pattern = iso15693_bit_patterns[symbol];
            EMIT_BITS(pattern, 8);
        }
    }

    /* EOF */
    EMIT_BITS(ISO15693_EOF_PATTERN, ISO15693_EOF_BITS);

    /* Flush remaining bits */
    if (bit_count > 0) {
        bit_accum <<= (8 - bit_count);
        encoded_buf[byte_pos++] = bit_accum;
    }

#undef EMIT_BITS

    return bit_pos;
}

/**
 * @brief Decode a received ISO15693 subcarrier stream back into data.
 *
 * @param encoded_data  Raw received subcarrier data
 * @param encoded_bits  Number of bits received
 * @param decoded_buf   Output decoded data
 * @param decoded_size  Size of output buffer
 * @param decoded_bits  Output: number of decoded bits
 * @return              NfcError
 */
static NfcError nfc_iso15693_decode_frame(
    const uint8_t* encoded_data,
    size_t encoded_bits,
    uint8_t* decoded_buf,
    size_t decoded_size,
    size_t* decoded_bits) {

    if (encoded_bits < ISO15693_RX_SOF_BITS + ISO15693_RX_EOF_BITS) {
        return NfcErrorDataFormat;
    }

    /* Helper: get bit at position */
#define GET_BIT(pos) ((encoded_data[(pos)/8] >> (7 - ((pos)%8))) & 1)

    size_t pos = 0;

    /* Check SOF */
    uint8_t sof = 0;
    for (int i = 0; i < ISO15693_RX_SOF_BITS; i++) {
        sof = (sof << 1) | GET_BIT(pos);
        pos++;
    }
    if (sof != ISO15693_RX_SOF_PATTERN) {
        return NfcErrorDataFormat;
    }

    /* Decode data symbols (2-of-8 coding: each data byte = 8 received bits for 2 data bits) */
    size_t dec_byte_pos = 0;
    size_t dec_bit_pos = 0;
    uint8_t current_byte = 0;
    int bits_in_byte = 0;

    while (pos + 8 <= encoded_bits) {
        /* Peek ahead for EOF */
        if (pos + ISO15693_RX_EOF_BITS <= encoded_bits) {
            uint8_t maybe_eof = 0;
            for (int i = 0; i < ISO15693_RX_EOF_BITS; i++) {
                maybe_eof = (maybe_eof << 1) | GET_BIT(pos + i);
            }
            if (maybe_eof == ISO15693_RX_EOF_PATTERN) {
                break; /* End of frame */
            }
        }

        uint8_t symbol = 0;
        for (int i = 0; i < 8; i++) {
            symbol = (symbol << 1) | GET_BIT(pos);
            pos++;
        }

        /* Decode the 8-bit pattern to 2 data bits */
        int decoded = -1;
        for (int j = 0; j < 4; j++) {
            if (symbol == iso15693_bit_patterns[j]) {
                decoded = j;
                break;
            }
        }

        if (decoded < 0) {
            return NfcErrorDataFormat;
        }

        current_byte |= (decoded << bits_in_byte);
        bits_in_byte += 2;
        dec_bit_pos += 2;

        if (bits_in_byte == 8) {
            if (dec_byte_pos >= decoded_size) {
                return NfcErrorBufferOverflow;
            }
            decoded_buf[dec_byte_pos++] = current_byte;
            current_byte = 0;
            bits_in_byte = 0;
        }
    }

    /* Handle residual bits */
    if (bits_in_byte > 0) {
        if (dec_byte_pos >= decoded_size) {
            return NfcErrorBufferOverflow;
        }
        decoded_buf[dec_byte_pos++] = current_byte;
    }

    *decoded_bits = dec_bit_pos;

#undef GET_BIT

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Common Init                                                               */
/* ========================================================================== */

static NfcError nfc_iso15693_common_init(void) {
    /* RX config for ISO15693: 12kHz BW, low-pass 600kHz */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF1,
        ST25R3916_REG_RX_CONF1_z12k | ST25R3916_REG_RX_CONF1_h80 |
        ST25R3916_REG_RX_CONF1_lp_600khz);
    /* Correlator config for 424kHz single subcarrier */
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, ST25R3916_REG_CORR_CONF2_corr_s8);

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller                                                                    */
/* ========================================================================== */

static NfcError nfc_iso15693_poller_init(void) {
    /* Subcarrier stream mode, OOK modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_om_mask | ST25R3916_REG_MODE_tr_am,
        ST25R3916_REG_MODE_om_subcarrier_stream | ST25R3916_REG_MODE_tr_am_ook);

    /* Stream mode config: subcarrier 424kHz, TX bit rate 1/4 (106), 8 pulses per symbol */
    st25r3916_write_reg(ST25R3916_REG_STREAM_MODE,
        ST25R3916_REG_STREAM_MODE_scf_sc424 |
        ST25R3916_REG_STREAM_MODE_stx_106 |
        ST25R3916_REG_STREAM_MODE_scp_8pulses);

    /* Clear AUX_MOD AM disable */
    st25r3916_clear_reg_bits(ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_AUX_MOD_dis_reg_am);

    return nfc_iso15693_common_init();
}

static NfcError nfc_iso15693_poller_deinit(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller TX/RX with frame encoding/decoding                                */
/* ========================================================================== */

static NfcError nfc_iso15693_poller_tx(
    const uint8_t* tx_data,
    size_t tx_bits) {

    size_t tx_bytes = (tx_bits + 7) / 8;
    size_t encoded_bits = nfc_iso15693_encode_frame(tx_data, tx_bytes, iso15693_fifo_buf);

    return nfc_common_fifo_tx(iso15693_fifo_buf, encoded_bits);
}

static NfcError nfc_iso15693_poller_rx(
    uint8_t* rx_data,
    size_t rx_data_size,
    size_t* rx_bits) {

    /* Read raw subcarrier data from FIFO */
    size_t raw_bits = 0;
    NfcError error = nfc_common_fifo_rx(iso15693_fifo_buf, sizeof(iso15693_fifo_buf), &raw_bits);
    if (error != NfcErrorNone) return error;

    /* Decode subcarrier stream to data */
    return nfc_iso15693_decode_frame(iso15693_fifo_buf, raw_bits, rx_data, rx_data_size, rx_bits);
}

/* ========================================================================== */
/*  Listener (stubbed — requires transparent mode)                            */
/* ========================================================================== */

static NfcError nfc_iso15693_listener_init(void) {
    ESP_LOGW(TAG, "ISO15693 listener mode not supported (requires transparent mode)");
    return NfcErrorInternal;
}

static NfcError nfc_iso15693_listener_deinit(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Tech Base Structure                                                       */
/* ========================================================================== */

const NfcTechBase nfc_tech_iso15693 = {
    .poller = {
        .compensation = {
            .fdt = NFC_POLLER_FDT_COMP_FC,
            .fwt = NFC_ISO15693_POLLER_FWT_COMP_FC,
        },
        .init       = nfc_iso15693_poller_init,
        .deinit     = nfc_iso15693_poller_deinit,
        .wait_event = nfc_wait_event_common,
        .tx         = nfc_iso15693_poller_tx,
        .rx         = nfc_iso15693_poller_rx,
    },
    .listener = {
        .compensation = {
            .fdt = NFC_ISO15693_LISTENER_FDT_FC,
        },
        .init       = nfc_iso15693_listener_init,
        .deinit     = nfc_iso15693_listener_deinit,
        .wait_event = NULL,
        .tx         = NULL,
        .rx         = NULL,
        .sleep      = NULL,
        .idle       = NULL,
    },
};
