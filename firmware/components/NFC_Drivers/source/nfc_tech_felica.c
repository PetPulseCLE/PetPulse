/**
 * @file nfc_tech_felica.c
 * @brief FeliCa (NFC-F) technology implementation (ported from Flipper Zero).
 */

#include "nfc_driver.h"
#include "st25r3916_driver.h"
#include <string.h>
#include "esp_log.h"

#define TAG "NfcFelica"

/** FDT compensation for FeliCa listener (prevent timer from starting) */
#define NFC_FELICA_LISTENER_FDT_COMP_FC (INT32_MAX)

/* ========================================================================== */
/*  PT-F Memory Structure (packed)                                            */
/* ========================================================================== */

typedef struct __attribute__((packed)) {
    uint16_t system_code;
    uint8_t  response_code;
    uint8_t  idm[8];
    uint8_t  pmm[8];
    uint16_t communication_performance;
} FuriHalFelicaPtMemory;

_Static_assert(sizeof(FuriHalFelicaPtMemory) == ST25R3916_PTM_F_LEN,
    "FelicaPtMemory must match PT-F memory size");

/* ========================================================================== */
/*  Common Init                                                               */
/* ========================================================================== */

static NfcError nfc_felica_common_init(void) {
    /* RX config for FeliCa 212 kbps */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF1, ST25R3916_REG_RX_CONF1_z600k);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF2_agc6_3 | ST25R3916_REG_RX_CONF2_agc_m |
        ST25R3916_REG_RX_CONF2_agc_en | ST25R3916_REG_RX_CONF2_sqm_dyn);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF3, 0x00);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF4, 0x00);
    /* Correlator */
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF1,
        ST25R3916_REG_CORR_CONF1_corr_s0 | ST25R3916_REG_CORR_CONF1_corr_s4 |
        ST25R3916_REG_CORR_CONF1_corr_s6);
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, 0x00);

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller                                                                    */
/* ========================================================================== */

static NfcError nfc_felica_poller_init(void) {
    /* FeliCa mode, AM modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_om_mask | ST25R3916_REG_MODE_tr_am,
        ST25R3916_REG_MODE_om_felica | ST25R3916_REG_MODE_tr_am_am);

    /* AM modulation depth 10% */
    st25r3916_change_reg_bits(
        ST25R3916_REG_TX_DRIVER,
        ST25R3916_REG_TX_DRIVER_am_mod_mask,
        ST25R3916_REG_TX_DRIVER_am_mod_10percent);

    st25r3916_clear_reg_bits(ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_AUX_MOD_dis_reg_am);

    /* Bit rate: TX 212 kbps, RX 212 kbps */
    st25r3916_write_reg(ST25R3916_REG_BIT_RATE,
        ST25R3916_REG_BIT_RATE_txrate_212 | ST25R3916_REG_BIT_RATE_rxrate_212);

    return nfc_felica_common_init();
}

static NfcError nfc_felica_poller_deinit(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Listener                                                                  */
/* ========================================================================== */

static NfcError nfc_felica_listener_init(void) {
    /* Set default registers first */
    st25r3916_direct_cmd(ST25R3916_CMD_SET_DEFAULT);

    /* Enable operating control */
    st25r3916_write_reg(ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
        ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

    /* Target mode, FeliCa, AM modulation */
    st25r3916_write_reg(ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_targ_targ | ST25R3916_REG_MODE_om2 | ST25R3916_REG_MODE_tr_am);

    /* Bit rate 212/212 */
    st25r3916_write_reg(ST25R3916_REG_BIT_RATE,
        ST25R3916_REG_BIT_RATE_txrate_212 | ST25R3916_REG_BIT_RATE_rxrate_212);

    /* RX config */
    nfc_felica_common_init();

    /* TX driver: AM modulation 40% for listener */
    st25r3916_change_reg_bits(
        ST25R3916_REG_TX_DRIVER,
        ST25R3916_REG_TX_DRIVER_am_mod_mask,
        ST25R3916_REG_TX_DRIVER_am_mod_40percent);

    /* Mask RX timer */
    st25r3916_write_reg(ST25R3916_REG_MASK_RX_TIMER, 0x02);

    /* Clear and set interrupts */
    st25r3916_direct_cmd(ST25R3916_CMD_STOP);

    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE |
         ST25R3916_IRQ_MASK_EON | ST25R3916_IRQ_MASK_EOF | ST25R3916_IRQ_MASK_WU_F);
    st25r3916_get_irq();
    st25r3916_mask_irq(~interrupts);

    /* Passive target register config */
    st25r3916_write_reg(ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_fdel_2 | ST25R3916_REG_PASSIVE_TARGET_fdel_0);

    /* Start sensing */
    st25r3916_direct_cmd(ST25R3916_CMD_GOTO_SENSE);

    return NfcErrorNone;
}

static NfcError nfc_felica_listener_deinit(void) {
    return NfcErrorNone;
}

static NfcEvent nfc_felica_listener_wait_event(uint32_t timeout_ms) {
    return nfc_wait_event_common(timeout_ms);
}

static NfcError nfc_felica_listener_tx(
    const uint8_t* tx_data,
    size_t tx_bits) {

    NfcError error = nfc_common_fifo_tx(tx_data, tx_bits);
    if (error != NfcErrorNone) return error;

    /* Wait for TX end */
    bool tx_end = nfc_event_wait_for_specific_irq(ST25R3916_IRQ_MASK_TXE, 10);
    if (!tx_end) {
        return NfcErrorCommunicationTimeout;
    }

    return NfcErrorNone;
}

static NfcError nfc_felica_listener_sleep(void) {
    return NfcErrorNone;
}

static NfcError nfc_felica_listener_idle(void) {
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Public Functions                                                          */
/* ========================================================================== */

NfcError nfc_felica_listener_set_sensf_res_data(
    const uint8_t* idm,
    uint8_t idm_len,
    const uint8_t* pmm,
    uint8_t pmm_len,
    uint16_t sys_code) {

    FuriHalFelicaPtMemory pt_mem;
    memset(&pt_mem, 0, sizeof(pt_mem));
    pt_mem.system_code = sys_code;
    pt_mem.response_code = 0x01;
    memcpy(pt_mem.idm, idm, idm_len < 8 ? idm_len : 8);
    memcpy(pt_mem.pmm, pmm, pmm_len < 8 ? pmm_len : 8);
    pt_mem.communication_performance = 0x0000;

    st25r3916_write_ptf_mem((const uint8_t*)&pt_mem, sizeof(pt_mem));

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Tech Base Structure                                                       */
/* ========================================================================== */

const NfcTechBase nfc_tech_felica = {
    .poller = {
        .compensation = {
            .fdt = NFC_POLLER_FDT_COMP_FC,
            .fwt = NFC_POLLER_FWT_COMP_FC,
        },
        .init       = nfc_felica_poller_init,
        .deinit     = nfc_felica_poller_deinit,
        .wait_event = nfc_wait_event_common,
        .tx         = nfc_poller_tx_common,
        .rx         = nfc_common_fifo_rx,
    },
    .listener = {
        .compensation = {
            .fdt = NFC_FELICA_LISTENER_FDT_COMP_FC,
        },
        .init       = nfc_felica_listener_init,
        .deinit     = nfc_felica_listener_deinit,
        .wait_event = nfc_felica_listener_wait_event,
        .tx         = nfc_felica_listener_tx,
        .rx         = nfc_common_fifo_rx,
        .sleep      = nfc_felica_listener_sleep,
        .idle       = nfc_felica_listener_idle,
    },
};
