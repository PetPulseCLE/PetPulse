/**
 * @file nfc_tech_iso14443a.c
 * @brief ISO14443A (NFC-A) technology implementation (ported from Flipper Zero).
 */

#include "nfc_driver.h"
#include "st25r3916_driver.h"
#include <string.h>
#include "esp_log.h"

#define TAG "NfcIso14443a"

/** Prevent FDT timer from starting in listener mode */
#define NFC_ISO14443A_LISTENER_FDT_COMP_FC (INT32_MAX)

/* ========================================================================== */
/*  Common Init (shared between poller and listener)                          */
/* ========================================================================== */

static NfcError nfc_iso14443a_common_init(void) {
    /* 1st stage zero = 600kHz, 3rd stage zero = 200 kHz */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF1, ST25R3916_REG_RX_CONF1_z600k);
    /* AGC enabled, ratio 3:1, squelch after TX */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF2_agc6_3 | ST25R3916_REG_RX_CONF2_agc_m |
        ST25R3916_REG_RX_CONF2_agc_en | ST25R3916_REG_RX_CONF2_sqm_dyn);
    /* HF operation, full gain */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF3, 0x00);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF4, 0x00);
    /* Correlator config */
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF1,
        ST25R3916_REG_CORR_CONF1_corr_s0 | ST25R3916_REG_CORR_CONF1_corr_s4 |
        ST25R3916_REG_CORR_CONF1_corr_s6);
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, 0x00);

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller                                                                    */
/* ========================================================================== */

static NfcError nfc_iso14443a_poller_init(void) {
    /* ISO14443A mode, OOK modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_om_mask | ST25R3916_REG_MODE_tr_am,
        ST25R3916_REG_MODE_om_iso14443a | ST25R3916_REG_MODE_tr_am_ook);

    /* Over/undershoot protection */
    st25r3916_change_reg_bits(ST25R3916_REG_OVERSHOOT_CONF1, 0xFF, 0x40);
    st25r3916_change_reg_bits(ST25R3916_REG_OVERSHOOT_CONF2, 0xFF, 0x03);
    st25r3916_change_reg_bits(ST25R3916_REG_UNDERSHOOT_CONF1, 0xFF, 0x40);
    st25r3916_change_reg_bits(ST25R3916_REG_UNDERSHOOT_CONF2, 0xFF, 0x03);

    return nfc_iso14443a_common_init();
}

static NfcError nfc_iso14443a_poller_deinit(void) {
    /* Reset parity bits to normal */
    st25r3916_change_reg_bits(
        ST25R3916_REG_ISO14443A_NFC,
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par),
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par_off | ST25R3916_REG_ISO14443A_NFC_no_rx_par_off));
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Listener                                                                  */
/* ========================================================================== */

static NfcError nfc_iso14443a_listener_init(void) {
    /* NOTE: Flipper allocates iso14443_3a_signal for transparent mode TX.
     * This is Flipper-specific GPIO bit-banging and cannot be directly ported.
     * Listener TX will use FIFO-based transmission instead. */

    st25r3916_write_reg(ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
        ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

    st25r3916_write_reg(ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_targ_targ | ST25R3916_REG_MODE_om0);

    st25r3916_write_reg(ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_fdel_2 | ST25R3916_REG_PASSIVE_TARGET_fdel_0 |
        ST25R3916_REG_PASSIVE_TARGET_d_ac_ap2p | ST25R3916_REG_PASSIVE_TARGET_d_212_424_1r);

    st25r3916_write_reg(ST25R3916_REG_MASK_RX_TIMER, 0x02);

    st25r3916_direct_cmd(ST25R3916_CMD_STOP);

    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE |
         ST25R3916_IRQ_MASK_EON | ST25R3916_IRQ_MASK_EOF | ST25R3916_IRQ_MASK_WU_A_X |
         ST25R3916_IRQ_MASK_WU_A);
    st25r3916_get_irq();
    st25r3916_mask_irq(~interrupts);

    /* Enable auto collision resolution */
    st25r3916_clear_reg_bits(
        ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);

    st25r3916_direct_cmd(ST25R3916_CMD_GOTO_SENSE);

    return nfc_iso14443a_common_init();
}

static NfcError nfc_iso14443a_listener_deinit(void) {
    /* No deallocation needed (no transparent mode signal) */
    return NfcErrorNone;
}

static NfcEvent nfc_iso14443a_listener_wait_event(uint32_t timeout_ms) {
    NfcEvent event = nfc_wait_event_common(timeout_ms);

    if (event & NFC_EVENT_LISTENER_ACTIVE) {
        /* Disable auto collision resolution once active */
        st25r3916_set_reg_bits(
            ST25R3916_REG_PASSIVE_TARGET,
            ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    }

    return event;
}

/* ========================================================================== */
/*  Listener TX (FIFO-based, replaces Flipper's transparent mode)             */
/* ========================================================================== */

static NfcError nfc_iso14443a_listener_tx(
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

static NfcError nfc_iso14443a_listener_sleep(void) {
    /* Enable auto collision resolution */
    st25r3916_clear_reg_bits(
        ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    st25r3916_direct_cmd(ST25R3916_CMD_STOP);
    st25r3916_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
    return NfcErrorNone;
}

static NfcError nfc_iso14443a_listener_idle(void) {
    /* Enable auto collision resolution */
    st25r3916_clear_reg_bits(
        ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    st25r3916_direct_cmd(ST25R3916_CMD_STOP);
    st25r3916_direct_cmd(ST25R3916_CMD_GOTO_SENSE);
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Public Functions                                                          */
/* ========================================================================== */

NfcError nfc_iso14443a_poller_trx_short_frame(NfcIso14443aShortFrame frame) {
    /* Disable CRC check */
    st25r3916_set_reg_bits(ST25R3916_REG_AUX, ST25R3916_REG_AUX_no_crc_rx);
    st25r3916_change_reg_bits(
        ST25R3916_REG_ISO14443A_NFC,
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par),
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par_off | ST25R3916_REG_ISO14443A_NFC_no_rx_par_off));

    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES2, 0);

    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE);
    st25r3916_get_irq();
    st25r3916_mask_irq(~interrupts);

    if (frame == NfcIso14443aShortFrameAllReq) {
        st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_REQA);
    } else {
        st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_WUPA);
    }

    return NfcErrorNone;
}

NfcError nfc_iso14443a_tx_sdd_frame(const uint8_t* tx_data, size_t tx_bits) {
    return nfc_poller_tx(tx_data, tx_bits);
}

NfcError nfc_iso14443a_rx_sdd_frame(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    return nfc_poller_rx(rx_data, rx_data_size, rx_bits);
}

NfcError nfc_iso14443a_poller_tx_custom_parity(const uint8_t* tx_data, size_t tx_bits) {
    /* Prepare TX with custom parity (no_tx_par + no_rx_par enabled) */
    st25r3916_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_clear_reg_bits(
        ST25R3916_REG_TIMER_EMV_CONTROL,
        ST25R3916_REG_TIMER_EMV_CONTROL_nrt_emv);
    st25r3916_change_reg_bits(
        ST25R3916_REG_ISO14443A_NFC,
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par),
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par));

    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE);
    st25r3916_get_irq();
    st25r3916_mask_irq(~interrupts);

    st25r3916_write_fifo(tx_data, tx_bits);
    st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);

    return NfcErrorNone;
}

NfcError nfc_iso14443a_listener_set_col_res_data(
    uint8_t* uid,
    uint8_t uid_len,
    uint8_t* atqa,
    uint8_t sak) {

    /* Set 4 or 7 bytes UID */
    if (uid_len == 4) {
        st25r3916_change_reg_bits(
            ST25R3916_REG_AUX,
            ST25R3916_REG_AUX_nfc_id_mask,
            ST25R3916_REG_AUX_nfc_id_4bytes);
    } else {
        st25r3916_change_reg_bits(
            ST25R3916_REG_AUX,
            ST25R3916_REG_AUX_nfc_id_mask,
            ST25R3916_REG_AUX_nfc_id_7bytes);
    }

    /* Write PT Memory A (15 bytes) */
    uint8_t pt_memory[15] = {0};
    memcpy(pt_memory, uid, uid_len);
    pt_memory[10] = atqa[0];
    pt_memory[11] = atqa[1];
    if (uid_len == 4) {
        pt_memory[12] = sak & ~0x04;
    } else {
        pt_memory[12] = 0x04;
    }
    pt_memory[13] = sak & ~0x04;
    pt_memory[14] = sak & ~0x04;

    st25r3916_write_pta_mem(pt_memory, sizeof(pt_memory));

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Tech Base Structure                                                       */
/* ========================================================================== */

const NfcTechBase nfc_tech_iso14443a = {
    .poller = {
        .compensation = {
            .fdt = NFC_POLLER_FDT_COMP_FC,
            .fwt = NFC_POLLER_FWT_COMP_FC,
        },
        .init       = nfc_iso14443a_poller_init,
        .deinit     = nfc_iso14443a_poller_deinit,
        .wait_event = nfc_wait_event_common,
        .tx         = nfc_poller_tx_common,
        .rx         = nfc_common_fifo_rx,
    },
    .listener = {
        .compensation = {
            .fdt = NFC_ISO14443A_LISTENER_FDT_COMP_FC,
        },
        .init       = nfc_iso14443a_listener_init,
        .deinit     = nfc_iso14443a_listener_deinit,
        .wait_event = nfc_iso14443a_listener_wait_event,
        .tx         = nfc_iso14443a_listener_tx,
        .rx         = nfc_common_fifo_rx,
        .sleep      = nfc_iso14443a_listener_sleep,
        .idle       = nfc_iso14443a_listener_idle,
    },
};
