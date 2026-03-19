/**
 * @file nfc_pairing.c
 * @brief NFC tap-to-pair implementation — Type 4 Tag emulation over ISO-DEP.
 *
 * Implements the full NFC Forum Type 4 Tag Operation Specification:
 *  - ISO14443A listener mode (auto collision resolution via PT Memory A)
 *  - ISO-DEP (ISO14443-4) framing: RATS/ATS, I-Block, R-Block, S-Block
 *  - NDEF Tag Application (AID D2760000850101)
 *  - Capability Container (CC) file
 *  - NDEF file serving BLE Handover Select data
 *
 * When a phone taps the NFC antenna:
 *  1. ST25R3916 auto-handles anticollision (ATQA/UID/SAK from PT Memory A)
 *  2. Phone sends RATS → we respond with ATS
 *  3. Phone selects NDEF application → reads CC → reads NDEF file
 *  4. NDEF contains BLE address + device name → phone connects via BLE
 *  5. BLE pairing completes (Just Works or OOB)
 */

#include "nfc_pairing.h"
#include "nfc_driver.h"
#include "nfc_ndef.h"
#include "ble_driver.h"
#include "st25r3916_driver.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "NfcPairing"

/* ========================================================================== */
/*  Type 4 Tag Constants                                                      */
/* ========================================================================== */

/** NDEF Tag Application AID (NFC Forum) */
static const uint8_t NDEF_APP_AID[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

/** CC (Capability Container) file ID */
#define CC_FILE_ID      0xE103

/** NDEF file ID */
#define NDEF_FILE_ID    0xE104

/** Maximum frame size for card (FSD=256, FSCI=8) */
#define T4T_FSD         256

/** Max NDEF file size (including 2-byte NLEN prefix) */
#define T4T_NDEF_FILE_MAX  (NDEF_MSG_MAX_SIZE + 2)

/** Capability Container file (15 bytes) */
static const uint8_t cc_file[] = {
    0x00, 0x0F,     /* CCLEN = 15 */
    0x20,           /* Mapping version 2.0 */
    0x00, 0xF6,     /* MLe (max R-APDU data) = 246 */
    0x00, 0xF6,     /* MLc (max C-APDU data) = 246 */
    /* NDEF File Control TLV */
    0x04,           /* T = NDEF Message TLV */
    0x06,           /* L = 6 */
    0xE1, 0x04,     /* File ID = E104 */
    0x01, 0xF4,     /* Max NDEF file size = 500 bytes */
    0x00,           /* Read access = free (no security) */
    0xFF,           /* Write access = denied */
};

/* APDU INS codes */
#define APDU_INS_SELECT     0xA4
#define APDU_INS_READ       0xB0
#define APDU_INS_UPDATE     0xD6

/* SW (Status Word) codes */
#define SW_OK_HI            0x90
#define SW_OK_LO            0x00
#define SW_NOT_FOUND_HI     0x6A
#define SW_NOT_FOUND_LO     0x82
#define SW_WRONG_P1P2_HI    0x6A
#define SW_WRONG_P1P2_LO    0x86
#define SW_WRONG_LENGTH_HI  0x67
#define SW_WRONG_LENGTH_LO  0x00
#define SW_INS_NOT_SUP_HI   0x6D
#define SW_INS_NOT_SUP_LO   0x00
#define SW_CLA_NOT_SUP_HI   0x6E
#define SW_CLA_NOT_SUP_LO   0x00

/* ISO-DEP block types (PCB byte masks) */
#define ISODEP_I_BLOCK_MASK   0xE2  /* 0bxxx000x0 pattern for I-block */
#define ISODEP_I_BLOCK_VAL    0x02
#define ISODEP_R_BLOCK_MASK   0xE6  /* 0bxxx001x0 pattern */
#define ISODEP_R_BLOCK_VAL    0xA2
#define ISODEP_S_BLOCK_MASK   0xC7  /* 0b11xxx1x0 */
#define ISODEP_S_DESELECT     0xC2
#define ISODEP_S_WTX          0xF2

/* ========================================================================== */
/*  Internal State                                                            */
/* ========================================================================== */

typedef enum {
    T4T_STATE_NONE,         /**< No application selected */
    T4T_STATE_APP_SELECTED, /**< NDEF application selected */
    T4T_STATE_CC_SELECTED,  /**< CC file selected */
    T4T_STATE_NDEF_SELECTED,/**< NDEF file selected */
} t4t_state_t;

static struct {
    TaskHandle_t        task_handle;
    volatile bool       running;
    volatile bool       stop_requested;
    nfc_pairing_config_t config;

    /* NDEF data */
    ndef_message_t      ndef_msg;
    uint8_t             ndef_file[T4T_NDEF_FILE_MAX]; /**< [NLEN_hi, NLEN_lo, NDEF...] */
    size_t              ndef_file_len;

    /* ISO-DEP state */
    t4t_state_t         t4t_state;
    uint8_t             block_number;   /**< ISO-DEP block number toggle */

    /* Buffers */
    uint8_t             rx_buf[T4T_FSD];
    uint8_t             tx_buf[T4T_FSD];
} s_pair;

/* ========================================================================== */
/*  Forward Declarations                                                      */
/* ========================================================================== */

static void nfc_pairing_task(void *param);
static bool build_ndef_data(void);
static bool handle_rats(const uint8_t *rx, size_t rx_len);
static bool handle_isodep_frame(const uint8_t *rx, size_t rx_len);
static size_t process_apdu(const uint8_t *capdu, size_t capdu_len,
                           uint8_t *rapdu, size_t rapdu_max);
static bool isodep_tx(const uint8_t *data, size_t len);

/* ========================================================================== */
/*  NDEF Data Builder                                                         */
/* ========================================================================== */

static bool build_ndef_data(void)
{
    uint8_t ble_addr[6];
    uint8_t addr_type;

    esp_err_t err = ble_driver_get_address(ble_addr, &addr_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot get BLE address (BLE not ready?)");
        return false;
    }

    const char *name = ble_driver_get_device_name();
    uint16_t appearance = ble_driver_get_appearance();

    /* Optionally generate OOB TK for secure pairing */
    uint8_t tk[16];
    uint8_t *tk_ptr = NULL;
    if (s_pair.config.include_oob_tk) {
        if (ble_driver_generate_oob_tk(tk) == ESP_OK) {
            tk_ptr = tk;
        }
    }

    /* Build NDEF Handover Select message */
    size_t ndef_len = ndef_build_ble_handover_select(
        &s_pair.ndef_msg, ble_addr, addr_type, name, appearance, tk_ptr);

    if (ndef_len == 0) {
        ESP_LOGE(TAG, "Failed to build NDEF message");
        return false;
    }

    /* Wrap in Type 4 Tag NDEF file format: [NLEN_hi, NLEN_lo, NDEF...] */
    if (ndef_len + 2 > T4T_NDEF_FILE_MAX) {
        ESP_LOGE(TAG, "NDEF too large for T4T file");
        return false;
    }

    s_pair.ndef_file[0] = (uint8_t)((ndef_len >> 8) & 0xFF);
    s_pair.ndef_file[1] = (uint8_t)(ndef_len & 0xFF);
    memcpy(&s_pair.ndef_file[2], s_pair.ndef_msg.data, ndef_len);
    s_pair.ndef_file_len = ndef_len + 2;

    ESP_LOGI(TAG, "NDEF file built: %u bytes (NDEF msg: %u bytes)",
             (unsigned)s_pair.ndef_file_len, (unsigned)ndef_len);
    ESP_LOGI(TAG, "  BLE addr: %02X:%02X:%02X:%02X:%02X:%02X (type %d)",
             ble_addr[5], ble_addr[4], ble_addr[3],
             ble_addr[2], ble_addr[1], ble_addr[0], addr_type);
    ESP_LOGI(TAG, "  Name: \"%s\", OOB TK: %s", name,
             tk_ptr ? "yes" : "no");

    return true;
}

/* ========================================================================== */
/*  ISO-DEP Transmit Helper                                                   */
/* ========================================================================== */

/**
 * @brief Transmit an ISO-DEP frame (with CRC appended by chip).
 */
static bool isodep_tx(const uint8_t *data, size_t len)
{
    /* Clear FIFO, write data, transmit WITH CRC */
    st25r3916_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_write_fifo(data, len * 8);
    st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_WITH_CRC);

    /* Wait for TX complete */
    NfcEvent event = nfc_listener_wait_event(50);
    if (!(event & NFC_EVENT_TX_END)) {
        ESP_LOGW(TAG, "ISO-DEP TX timeout");
        return false;
    }

    return true;
}

/* ========================================================================== */
/*  RATS / ATS Handling                                                       */
/* ========================================================================== */

/**
 * @brief Handle RATS (Request for Answer To Select) from phone.
 *
 *  RATS format: [0xE0, param]
 *   - param bits 7-4: FSDI (max frame size phone can receive)
 *   - param bits 3-0: CID (Card Identifier, usually 0)
 *
 *  ATS format: [TL, T0, TA(1), TB(1), TC(1)]
 *   - TL: total ATS length including TL
 *   - T0: FSCI (bits 3-0) + presence of TA/TB/TC (bits 6-4)
 *   - TA(1): supported bit rates
 *   - TB(1): FWI | SFGI
 *   - TC(1): CID/NAD support
 */
static bool handle_rats(const uint8_t *rx, size_t rx_len)
{
    if (rx_len < 2 || rx[0] != 0xE0) return false;

    /* Build ATS */
    uint8_t ats[5];
    ats[0] = 0x05;      /* TL = 5 bytes */
    ats[1] = 0x78;      /* T0: FSCI=8 (256 bytes), TA+TB+TC present */
    ats[2] = 0x80;      /* TA(1): 106 kbps only, same in both directions */
    ats[3] = 0xA1;      /* TB(1): FWI=10 (~309 ms timeout), SFGI=1 */
    ats[4] = 0x00;      /* TC(1): no CID, no NAD */

    ESP_LOGD(TAG, "RATS received, sending ATS");
    return isodep_tx(ats, sizeof(ats));
}

/* ========================================================================== */
/*  APDU Processing                                                           */
/* ========================================================================== */

/**
 * @brief Process a C-APDU and build the R-APDU response.
 *
 * C-APDU: [CLA, INS, P1, P2, (Lc, Data...), (Le)]
 * R-APDU: [Data..., SW1, SW2]
 */
static size_t process_apdu(const uint8_t *capdu, size_t capdu_len,
                           uint8_t *rapdu, size_t rapdu_max)
{
    if (capdu_len < 4) {
        /* Malformed APDU */
        rapdu[0] = SW_CLA_NOT_SUP_HI;
        rapdu[1] = SW_CLA_NOT_SUP_LO;
        return 2;
    }

    uint8_t cla = capdu[0];
    uint8_t ins = capdu[1];
    uint8_t p1  = capdu[2];
    uint8_t p2  = capdu[3];

    /* Only class 0x00 supported */
    if (cla != 0x00) {
        rapdu[0] = SW_CLA_NOT_SUP_HI;
        rapdu[1] = SW_CLA_NOT_SUP_LO;
        return 2;
    }

    switch (ins) {

    case APDU_INS_SELECT:
    {
        /* ---- SELECT by AID (P1=0x04, P2=0x00) ---- */
        if (p1 == 0x04 && p2 == 0x00) {
            if (capdu_len < 5) goto sw_wrong_length;
            uint8_t lc = capdu[4];
            if (capdu_len < (size_t)(5 + lc)) goto sw_wrong_length;

            const uint8_t *aid = &capdu[5];
            if (lc == sizeof(NDEF_APP_AID) &&
                memcmp(aid, NDEF_APP_AID, sizeof(NDEF_APP_AID)) == 0) {
                ESP_LOGD(TAG, "NDEF App selected");
                s_pair.t4t_state = T4T_STATE_APP_SELECTED;
                rapdu[0] = SW_OK_HI;
                rapdu[1] = SW_OK_LO;
                return 2;
            }
            /* Unknown AID */
            rapdu[0] = SW_NOT_FOUND_HI;
            rapdu[1] = SW_NOT_FOUND_LO;
            return 2;
        }

        /* ---- SELECT by File ID (P1=0x00, P2=0x0C) ---- */
        if (p1 == 0x00 && p2 == 0x0C) {
            if (capdu_len < 5) goto sw_wrong_length;
            uint8_t lc = capdu[4];
            if (lc != 2 || capdu_len < 7) goto sw_wrong_length;

            uint16_t file_id = ((uint16_t)capdu[5] << 8) | capdu[6];

            if (file_id == CC_FILE_ID) {
                ESP_LOGD(TAG, "CC file selected");
                s_pair.t4t_state = T4T_STATE_CC_SELECTED;
                rapdu[0] = SW_OK_HI;
                rapdu[1] = SW_OK_LO;
                return 2;
            }
            if (file_id == NDEF_FILE_ID) {
                ESP_LOGD(TAG, "NDEF file selected");
                s_pair.t4t_state = T4T_STATE_NDEF_SELECTED;
                rapdu[0] = SW_OK_HI;
                rapdu[1] = SW_OK_LO;
                return 2;
            }

            rapdu[0] = SW_NOT_FOUND_HI;
            rapdu[1] = SW_NOT_FOUND_LO;
            return 2;
        }

        rapdu[0] = SW_WRONG_P1P2_HI;
        rapdu[1] = SW_WRONG_P1P2_LO;
        return 2;
    }

    case APDU_INS_READ:
    {
        /* ---- READ BINARY ---- */
        uint16_t offset = ((uint16_t)p1 << 8) | p2;

        /* Le is the last byte of the APDU */
        uint8_t le = 0;
        if (capdu_len == 5) {
            le = capdu[4];
        } else {
            /* No Le → read as much as possible */
            le = 0; /* 0 means 256 in ISO 7816 */
        }
        uint16_t read_len = (le == 0) ? 256 : le;

        const uint8_t *file_data = NULL;
        size_t file_len = 0;

        switch (s_pair.t4t_state) {
        case T4T_STATE_CC_SELECTED:
            file_data = cc_file;
            file_len  = sizeof(cc_file);
            break;
        case T4T_STATE_NDEF_SELECTED:
            file_data = s_pair.ndef_file;
            file_len  = s_pair.ndef_file_len;
            break;
        default:
            rapdu[0] = SW_NOT_FOUND_HI;
            rapdu[1] = SW_NOT_FOUND_LO;
            return 2;
        }

        if (offset >= file_len) {
            rapdu[0] = SW_WRONG_P1P2_HI;
            rapdu[1] = SW_WRONG_P1P2_LO;
            return 2;
        }

        size_t available = file_len - offset;
        if (read_len > available) read_len = available;
        if (read_len + 2 > rapdu_max) read_len = rapdu_max - 2;

        memcpy(rapdu, &file_data[offset], read_len);
        rapdu[read_len]     = SW_OK_HI;
        rapdu[read_len + 1] = SW_OK_LO;

        ESP_LOGD(TAG, "READ BINARY: offset=%u len=%u", offset, read_len);
        return read_len + 2;
    }

    case APDU_INS_UPDATE:
        /* UPDATE BINARY — not supported (read-only tag) */
        rapdu[0] = SW_INS_NOT_SUP_HI;
        rapdu[1] = SW_INS_NOT_SUP_LO;
        return 2;

    default:
        rapdu[0] = SW_INS_NOT_SUP_HI;
        rapdu[1] = SW_INS_NOT_SUP_LO;
        return 2;
    }

sw_wrong_length:
    rapdu[0] = SW_WRONG_LENGTH_HI;
    rapdu[1] = SW_WRONG_LENGTH_LO;
    return 2;
}

/* ========================================================================== */
/*  ISO-DEP Frame Handler                                                     */
/* ========================================================================== */

/**
 * @brief Handle a received ISO-DEP frame (I-Block / R-Block / S-Block).
 *
 * @return true to continue, false to end session
 */
static bool handle_isodep_frame(const uint8_t *rx, size_t rx_len)
{
    if (rx_len < 1) return false;

    uint8_t pcb = rx[0];

    /* ---- S-Block: DESELECT ---- */
    if ((pcb & 0xF7) == ISODEP_S_DESELECT) {
        ESP_LOGD(TAG, "S(DESELECT) received");
        uint8_t resp = ISODEP_S_DESELECT;
        isodep_tx(&resp, 1);
        return false; /* End session */
    }

    /* ---- S-Block: WTX (Waiting Time Extension) ---- */
    if ((pcb & 0xF7) == ISODEP_S_WTX) {
        /* Echo back WTX with same WTXM value */
        uint8_t resp[2];
        resp[0] = ISODEP_S_WTX;
        resp[1] = (rx_len >= 2) ? rx[1] : 0x01;
        isodep_tx(resp, 2);
        return true;
    }

    /* ---- R-Block: ACK/NAK ---- */
    if ((pcb & ISODEP_R_BLOCK_MASK) == ISODEP_R_BLOCK_VAL) {
        /* R(ACK) — phone acknowledges chained block, not used for us */
        /* R(NAK) — phone requesting retransmit, we don't do chaining */
        ESP_LOGD(TAG, "R-Block received: 0x%02X", pcb);
        return true;
    }

    /* ---- I-Block: Application data ---- */
    if ((pcb & ISODEP_I_BLOCK_MASK) == ISODEP_I_BLOCK_VAL) {
        /* Extract APDU from INF field (skip PCB byte) */
        const uint8_t *capdu = &rx[1];
        size_t capdu_len = rx_len - 1;

        if (capdu_len == 0) return true;

        /* Build response APDU */
        uint8_t rapdu[T4T_FSD - 4]; /* Leave room for PCB + CRC overhead */
        size_t rapdu_len = process_apdu(capdu, capdu_len, rapdu, sizeof(rapdu));

        /* Wrap in I-Block with toggled block number */
        s_pair.tx_buf[0] = 0x02 | (s_pair.block_number & 0x01);
        memcpy(&s_pair.tx_buf[1], rapdu, rapdu_len);

        bool ok = isodep_tx(s_pair.tx_buf, 1 + rapdu_len);

        /* Toggle block number */
        s_pair.block_number ^= 1;

        return ok;
    }

    ESP_LOGW(TAG, "Unknown ISO-DEP PCB: 0x%02X", pcb);
    return true;
}

/* ========================================================================== */
/*  Single Session Handler                                                    */
/* ========================================================================== */

/**
 * @brief Handle one complete NFC session (from listener activation to deselect/field off).
 */
static void handle_nfc_session(void)
{
    /* Reset ISO-DEP state */
    s_pair.t4t_state = T4T_STATE_NONE;
    s_pair.block_number = 0;

    enum {
        SESSION_WAIT_RATS,
        SESSION_ISO_DEP,
    } state = SESSION_WAIT_RATS;

    ESP_LOGI(TAG, "Phone detected — starting T4T session");

    while (!s_pair.stop_requested) {
        NfcEvent event = nfc_listener_wait_event(2000);

        /* Check for abort or field off */
        if (event & NFC_EVENT_ABORT_REQUEST) {
            ESP_LOGD(TAG, "Abort requested");
            break;
        }
        if (event & NFC_EVENT_FIELD_OFF) {
            ESP_LOGI(TAG, "Field off — session ended");
            break;
        }
        if (event == NFC_EVENT_TIMEOUT) {
            ESP_LOGD(TAG, "Session timeout");
            break;
        }

        if (event & NFC_EVENT_RX_END) {
            /* Read received data from FIFO */
            size_t rx_bits = 0;
            NfcError err = nfc_listener_rx(s_pair.rx_buf, sizeof(s_pair.rx_buf), &rx_bits);
            if (err != NfcErrorNone) {
                ESP_LOGW(TAG, "RX error: %d", err);
                /* Prepare for next frame */
                st25r3916_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
                st25r3916_direct_cmd(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
                continue;
            }

            size_t rx_len = rx_bits / 8;
            if (rx_len == 0) continue;

            switch (state) {
            case SESSION_WAIT_RATS:
                if (s_pair.rx_buf[0] == 0xE0) {
                    if (handle_rats(s_pair.rx_buf, rx_len)) {
                        state = SESSION_ISO_DEP;
                        /* Prepare to receive next frame */
                        st25r3916_direct_cmd(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
                    } else {
                        ESP_LOGW(TAG, "ATS TX failed");
                        return;
                    }
                } else {
                    ESP_LOGW(TAG, "Expected RATS, got 0x%02X", s_pair.rx_buf[0]);
                    return;
                }
                break;

            case SESSION_ISO_DEP:
                if (!handle_isodep_frame(s_pair.rx_buf, rx_len)) {
                    ESP_LOGI(TAG, "Session deselected");
                    return;
                }
                /* Prepare to receive next frame */
                st25r3916_direct_cmd(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
                break;
            }
        }

        /* Also handle TX_END (from our responses) — just continue the loop */
    }
}

/* ========================================================================== */
/*  Pairing Task                                                              */
/* ========================================================================== */

static void nfc_pairing_task(void *param)
{
    (void)param;

    /* Wait for BLE to be ready (synced + address available) */
    int retries = 0;
    while (retries < 50 && !s_pair.stop_requested) {
        uint8_t addr[6];
        if (ble_driver_get_address(addr, NULL) == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(100));
        retries++;
    }

    if (s_pair.stop_requested) goto exit;

    /* Build NDEF data with current BLE address */
    if (!build_ndef_data()) {
        ESP_LOGE(TAG, "Failed to build NDEF, stopping pairing task");
        goto exit;
    }

    s_pair.running = true;
    ESP_LOGI(TAG, "NFC pairing task started");

    while (!s_pair.stop_requested) {
        /* Enter listener mode */
        NfcError err = nfc_set_mode(NfcModeListener, NfcTechIso14443a);
        if (err != NfcErrorNone) {
            ESP_LOGE(TAG, "Failed to set listener mode: %d", err);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Configure collision resolution with our UID/ATQA/SAK */
        err = nfc_iso14443a_listener_set_col_res_data(
            s_pair.config.uid,
            s_pair.config.uid_len,
            s_pair.config.atqa,
            s_pair.config.sak);
        if (err != NfcErrorNone) {
            ESP_LOGE(TAG, "Failed to set PT Memory A: %d", err);
            nfc_reset_mode();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "Waiting for NFC tap...");

        /* Wait for phone tap (listener activation) */
        NfcEvent event = nfc_listener_wait_event(NFC_EVENT_WAIT_FOREVER);

        if (event & NFC_EVENT_ABORT_REQUEST) {
            ESP_LOGI(TAG, "Abort received");
            nfc_reset_mode();
            break;
        }

        if (event & NFC_EVENT_LISTENER_ACTIVE) {
            /* Phone has selected us — handle the Type 4 Tag session */
            handle_nfc_session();
        }

        /* Reset mode and restart */
        nfc_reset_mode();

        /* Brief delay before restarting listener */
        vTaskDelay(pdMS_TO_TICKS(200));
    }

exit:
    s_pair.running = false;
    ESP_LOGI(TAG, "NFC pairing task exiting");
    s_pair.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ========================================================================== */
/*  Public API                                                                */
/* ========================================================================== */

esp_err_t nfc_pairing_start(const nfc_pairing_config_t *config)
{
    if (s_pair.running || s_pair.task_handle != NULL) {
        ESP_LOGW(TAG, "Pairing already running");
        return ESP_ERR_INVALID_STATE;
    }

    /* Apply config */
    if (config) {
        s_pair.config = *config;
    } else {
        nfc_pairing_config_t def = NFC_PAIRING_CONFIG_DEFAULT();
        s_pair.config = def;
    }

    /* Validate */
    if (s_pair.config.uid_len != 4 && s_pair.config.uid_len != 7) {
        ESP_LOGE(TAG, "UID length must be 4 or 7");
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_pair.config.sak & 0x20) == 0) {
        ESP_LOGW(TAG, "SAK bit 5 not set — Type 4 Tag (ISO-DEP) may not work");
    }

    /* Fix defaults */
    if (s_pair.config.task_stack_size == 0) s_pair.config.task_stack_size = 8192;
    if (s_pair.config.task_priority == 0)   s_pair.config.task_priority = 5;

    s_pair.stop_requested = false;

    BaseType_t ret = xTaskCreate(
        nfc_pairing_task,
        "nfc_pair",
        s_pair.config.task_stack_size,
        NULL,
        s_pair.config.task_priority,
        &s_pair.task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pairing task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t nfc_pairing_stop(void)
{
    if (!s_pair.running && s_pair.task_handle == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping NFC pairing...");
    s_pair.stop_requested = true;

    /* Signal the NFC event system to wake the task */
    nfc_abort();

    /* Wait for task to exit */
    int timeout = 50; /* 5 seconds */
    while (s_pair.task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }

    if (s_pair.task_handle != NULL) {
        ESP_LOGW(TAG, "Pairing task did not exit in time");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "NFC pairing stopped");
    return ESP_OK;
}

bool nfc_pairing_is_running(void)
{
    return s_pair.running;
}

esp_err_t nfc_pairing_refresh_ndef(void)
{
    if (!s_pair.running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!build_ndef_data()) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "NDEF data refreshed");
    return ESP_OK;
}
