/**
 * @file nfc_tech_iso14443b.c
 * @brief ISO14443B (NFC-B) technology implementation (ported from Flipper Zero).
 */

#include "nfc_driver.h"
#include "st25r3916_driver.h"
#include <string.h>
#include "esp_log.h"

#define TAG "NfcIso14443b"

/* ========================================================================== */
/*  Common Init                                                               */
/* ========================================================================== */

static NfcError nfc_iso14443b_common_init(void) {
    /* RX configuration for ISO14443B */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF1, ST25R3916_REG_RX_CONF1_h200);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF2_agc6_3 | ST25R3916_REG_RX_CONF2_agc_alg |
        ST25R3916_REG_RX_CONF2_agc_m | ST25R3916_REG_RX_CONF2_agc_en |
        ST25R3916_REG_RX_CONF2_pulz_61 | ST25R3916_REG_RX_CONF2_sqm_dyn);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF3, 0x00);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF4, 0x00);
    /* Correlator config */
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF1,
        ST25R3916_REG_CORR_CONF1_corr_s0 | ST25R3916_REG_CORR_CONF1_corr_s1 |
        ST25R3916_REG_CORR_CONF1_corr_s3 | ST25R3916_REG_CORR_CONF1_corr_s4);
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, 0x00);

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller                                                                    */
/* ========================================================================== */

static NfcError nfc_iso14443b_poller_init(void) {
    /* ISO14443B mode, AM modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_om_mask | ST25R3916_REG_MODE_tr_am,
        ST25R3916_REG_MODE_om_iso14443b | ST25R3916_REG_MODE_tr_am_am);

    /* AM modulation depth 10% */
    st25r3916_change_reg_bits(
        ST25R3916_REG_TX_DRIVER,
        ST25R3916_REG_TX_DRIVER_am_mod_mask,
        ST25R3916_REG_TX_DRIVER_am_mod_10percent);

    st25r3916_clear_reg_bits(ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_AUX_MOD_dis_reg_am);

    /* ISO14443B-1 and B-2 settings */
    st25r3916_write_reg(ST25R3916_REG_ISO14443B_1, 0x00);
    st25r3916_write_reg(ST25R3916_REG_ISO14443B_2, 0x01);

    return nfc_iso14443b_common_init();
}

static NfcError nfc_iso14443b_poller_deinit(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Listener (not supported for ISO14443B)                                    */
/* ========================================================================== */

static NfcError nfc_iso14443b_listener_init(void) {
    ESP_LOGW(TAG, "ISO14443B listener mode not supported");
    return NfcErrorInternal;
}

static NfcError nfc_iso14443b_listener_deinit(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Tech Base Structure                                                       */
/* ========================================================================== */

const NfcTechBase nfc_tech_iso14443b = {
    .poller = {
        .compensation = {
            .fdt = NFC_POLLER_FDT_COMP_FC,
            .fwt = NFC_POLLER_FWT_COMP_FC,
        },
        .init       = nfc_iso14443b_poller_init,
        .deinit     = nfc_iso14443b_poller_deinit,
        .wait_event = nfc_wait_event_common,
        .tx         = nfc_poller_tx_common,
        .rx         = nfc_common_fifo_rx,
    },
    .listener = {
        .compensation = {
            .fdt = 0,
        },
        .init       = nfc_iso14443b_listener_init,
        .deinit     = nfc_iso14443b_listener_deinit,
        .wait_event = NULL,
        .tx         = NULL,
        .rx         = NULL,
        .sleep      = NULL,
        .idle       = NULL,
    },
};
