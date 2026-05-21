/**
 * @file nfc_driver.h
 * @brief NFC HAL driver for ESP-IDF with ST25R3916 (ported from Flipper Zero).
 *
 * Provides mode/tech dispatch, event system, timer system, field control,
 * poller/listener TX/RX, and common helper functions.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Error Codes                                                               */
/* ========================================================================== */

typedef enum {
    NfcErrorNone = 0,
    NfcErrorCommunication,
    NfcErrorCommunicationTimeout,
    NfcErrorTimeout,
    NfcErrorProtocol,
    NfcErrorInternal,
    NfcErrorBusy,
    NfcErrorOscillator,
    NfcErrorBufferOverflow,
    NfcErrorDataFormat,
    NfcErrorIncompleteFrame,
} NfcError;

/* ========================================================================== */
/*  Technology & Mode Enumerations                                            */
/* ========================================================================== */

typedef enum {
    NfcTechIso14443a,
    NfcTechIso14443b,
    NfcTechFelica,
    NfcTechIso15693,
    NfcTechNum,
} NfcTech;

typedef enum {
    NfcModePoller,
    NfcModeListener,
    NfcModeNum,
} NfcMode;

/* ========================================================================== */
/*  NFC Events (bitmask)                                                      */
/* ========================================================================== */

typedef uint32_t NfcEvent;

#define NFC_EVENT_NONE                ((NfcEvent)0)
#define NFC_EVENT_OSC_ON              ((NfcEvent)(1U << 0))
#define NFC_EVENT_TX_END              ((NfcEvent)(1U << 1))
#define NFC_EVENT_RX_START            ((NfcEvent)(1U << 2))
#define NFC_EVENT_RX_END              ((NfcEvent)(1U << 3))
#define NFC_EVENT_COLLISION           ((NfcEvent)(1U << 4))
#define NFC_EVENT_FIELD_ON            ((NfcEvent)(1U << 5))
#define NFC_EVENT_FIELD_OFF           ((NfcEvent)(1U << 6))
#define NFC_EVENT_LISTENER_ACTIVE     ((NfcEvent)(1U << 7))
#define NFC_EVENT_TIMER_FWT_EXPIRED   ((NfcEvent)(1U << 8))
#define NFC_EVENT_TIMER_BLOCK_TX_EXPIRED ((NfcEvent)(1U << 9))
#define NFC_EVENT_TIMEOUT             ((NfcEvent)(1U << 10))
#define NFC_EVENT_ABORT_REQUEST       ((NfcEvent)(1U << 11))

#define NFC_EVENT_WAIT_FOREVER (UINT32_MAX)

/* ========================================================================== */
/*  Internal Event Types (for EventGroup signaling)                           */
/* ========================================================================== */

typedef enum {
    NfcEventInternalAbort                  = (1U << 0),
    NfcEventInternalIrq                    = (1U << 1),
    NfcEventInternalTimerFwtExpired        = (1U << 2),
    NfcEventInternalTimerBlockTxExpired    = (1U << 3),
    NfcEventInternalTransparentDataReceived = (1U << 4),
} NfcEventInternalType;

#define NFC_EVENT_INTERNAL_ALL                \
    (NfcEventInternalAbort |                  \
     NfcEventInternalIrq |                    \
     NfcEventInternalTimerFwtExpired |        \
     NfcEventInternalTimerBlockTxExpired |     \
     NfcEventInternalTransparentDataReceived)

/* ========================================================================== */
/*  ISO14443A Short Frame Types                                               */
/* ========================================================================== */

typedef enum {
    NfcIso14443aShortFrameAllReq,
    NfcIso14443aShortFrameSensReq,
} NfcIso14443aShortFrame;

/* ========================================================================== */
/*  Compensation Structures                                                   */
/* ========================================================================== */

typedef struct {
    int32_t fdt; /**< Frame delay time compensation, in carrier cycles */
    int32_t fwt; /**< Frame wait time compensation, in carrier cycles */
} NfcPollerCompensation;

typedef struct {
    int32_t fdt; /**< Frame delay time compensation, in carrier cycles */
} NfcListenerCompensation;

/* ========================================================================== */
/*  Technology Function Pointer Typedefs                                      */
/* ========================================================================== */

/** Configure chip for a specific technology */
typedef NfcError (*NfcChipConfig)(void);

/** Transmit data */
typedef NfcError (*NfcTx)(const uint8_t* tx_data, size_t tx_bits);

/** Receive data */
typedef NfcError (*NfcRx)(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits);

/** Wait for event */
typedef NfcEvent (*NfcWaitEvent)(uint32_t timeout_ms);

/** Listener sleep */
typedef NfcError (*NfcSleep)(void);

/** Listener idle */
typedef NfcError (*NfcIdle)(void);

/* ========================================================================== */
/*  Technology Base Structures                                                */
/* ========================================================================== */

typedef struct {
    NfcPollerCompensation compensation;
    NfcChipConfig init;
    NfcChipConfig deinit;
    NfcWaitEvent wait_event;
    NfcTx tx;
    NfcRx rx;
} NfcTechPollerBase;

typedef struct {
    NfcListenerCompensation compensation;
    NfcChipConfig init;
    NfcChipConfig deinit;
    NfcWaitEvent wait_event;
    NfcTx tx;
    NfcRx rx;
    NfcSleep sleep;
    NfcIdle idle;
} NfcTechListenerBase;

typedef struct {
    NfcTechPollerBase poller;
    NfcTechListenerBase listener;
} NfcTechBase;

/* ========================================================================== */
/*  Global State                                                              */
/* ========================================================================== */

typedef struct {
    NfcMode mode;
    NfcTech tech;
} NfcState;

extern NfcState nfc_state;

/* ========================================================================== */
/*  Technology Declarations                                                   */
/* ========================================================================== */

extern const NfcTechBase nfc_tech_iso14443a;
extern const NfcTechBase nfc_tech_iso14443b;
extern const NfcTechBase nfc_tech_iso15693;
extern const NfcTechBase nfc_tech_felica;

extern const NfcTechBase* const nfc_tech_table[];

/* ========================================================================== */
/*  Compensation Constants                                                    */
/* ========================================================================== */

#define NFC_POLLER_FDT_COMP_FC  (-500)
#define NFC_POLLER_FWT_COMP_FC  (-500)

/* ========================================================================== */
/*  Core API                                                                  */
/* ========================================================================== */

/** Initialize the NFC driver (full chip init sequence) */
esp_err_t nfc_init(void);

/** Deinitialize the NFC driver */
void nfc_deinit(void);

/** Check if the HAL is ready (chip ID check) */
NfcError nfc_is_hal_ready(void);

/* ========================================================================== */
/*  Power & Mode Management                                                   */
/* ========================================================================== */

/** Enter low-power mode (stop oscillator, disable field, deinit timers/GPIO ISR) */
NfcError nfc_low_power_mode_start(void);

/** Exit low-power mode (re-init GPIO ISR, timers, turn on oscillator) */
NfcError nfc_low_power_mode_stop(void);

/** Set operating mode and technology (dispatches to tech init) */
NfcError nfc_set_mode(NfcMode mode, NfcTech tech);

/** Reset mode (dispatches to tech deinit, restores default register values) */
NfcError nfc_reset_mode(void);

/* ========================================================================== */
/*  Field Control                                                             */
/* ========================================================================== */

/** Start external field detection */
NfcError nfc_field_detect_start(void);

/** Stop external field detection */
NfcError nfc_field_detect_stop(void);

/** Check if external field is present */
bool nfc_field_is_present(void);

/** Turn on poller field (TX + RX enable) */
NfcError nfc_poller_field_on(void);

/* ========================================================================== */
/*  Event System                                                              */
/* ========================================================================== */

/** Initialize event system (create EventGroup) */
void nfc_event_init(void);

/** Start event system (store calling task handle, clear flags) */
NfcError nfc_event_start(void);

/** Stop event system */
NfcError nfc_event_stop(void);

/** Emit an internal event (from ISR or timer callback) */
void nfc_event_set(NfcEventInternalType event);

/** Emit an internal event from ISR context */
void nfc_event_set_isr(NfcEventInternalType event);

/** Request abort */
NfcError nfc_abort(void);

/** Wait for events, map IRQs to NfcEvents (common to all techs) */
NfcEvent nfc_wait_event_common(uint32_t timeout_ms);

/** Wait for a specific IRQ mask */
bool nfc_event_wait_for_specific_irq(uint32_t mask, uint32_t timeout_ms);

/* ========================================================================== */
/*  GPIO ISR                                                                  */
/* ========================================================================== */

/** Initialize GPIO ISR for NFC IRQ pin */
void nfc_init_gpio_isr(void);

/** Deinitialize GPIO ISR */
void nfc_deinit_gpio_isr(void);

/** Get accumulated IRQ bits (drains while GPIO high, per Flipper behavior) */
uint32_t nfc_get_irq(void);

/** Wait for IRQ signal on the semaphore (legacy, used internally) */
esp_err_t nfc_wait_for_irq(uint32_t timeout_ms);

/* ========================================================================== */
/*  Timer System                                                              */
/* ========================================================================== */

/** Initialize FWT and BlockTx timers */
void nfc_timers_init(void);

/** Deinitialize all timers */
void nfc_timers_deinit(void);

/** Start FWT timer (carrier cycles, with compensation) */
void nfc_timer_fwt_start(uint32_t time_fc);

/** Stop FWT timer */
void nfc_timer_fwt_stop(void);

/** Start block TX timer (carrier cycles, with compensation) */
void nfc_timer_block_tx_start(uint32_t time_fc);

/** Start block TX timer (microseconds) */
void nfc_timer_block_tx_start_us(uint32_t time_us);

/** Stop block TX timer */
void nfc_timer_block_tx_stop(void);

/** Check if block TX timer is running */
bool nfc_timer_block_tx_is_running(void);

/* ========================================================================== */
/*  Poller TX/RX                                                              */
/* ========================================================================== */

/** Common poller TX: clear FIFO, configure parity, write FIFO, transmit */
NfcError nfc_poller_tx_common(const uint8_t* tx_data, size_t tx_bits);

/** Dispatch poller TX to current tech */
NfcError nfc_poller_tx(const uint8_t* tx_data, size_t tx_bits);

/** Dispatch poller RX to current tech */
NfcError nfc_poller_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits);

/** Wait for poller event (dispatches to current tech) */
NfcEvent nfc_poller_wait_event(uint32_t timeout_ms);

/* ========================================================================== */
/*  Listener TX/RX                                                            */
/* ========================================================================== */

/** Wait for listener event (dispatches to current tech) */
NfcEvent nfc_listener_wait_event(uint32_t timeout_ms);

/** Dispatch listener TX to current tech */
NfcError nfc_listener_tx(const uint8_t* tx_data, size_t tx_bits);

/** Dispatch listener RX to current tech */
NfcError nfc_listener_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits);

/** Listener sleep (dispatches to current tech) */
NfcError nfc_listener_sleep(void);

/** Listener idle (dispatches to current tech) */
NfcError nfc_listener_idle(void);

/** Enable listener receive */
NfcError nfc_listener_enable_rx(void);

/* ========================================================================== */
/*  Common FIFO Helpers                                                       */
/* ========================================================================== */

/** Common FIFO TX: clear FIFO, write, transmit without CRC */
NfcError nfc_common_fifo_tx(const uint8_t* tx_data, size_t tx_bits);

/** Common FIFO RX: read FIFO with status check */
NfcError nfc_common_fifo_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits);

/** Common listener RX start: unmask receive */
NfcError nfc_common_listener_rx_start(void);

/** Reset TRX: send STOP command */
NfcError nfc_trx_reset(void);

/* ========================================================================== */
/*  ISO14443A Specific Functions                                              */
/* ========================================================================== */

/** Transmit short frame (REQA/WUPA) */
NfcError nfc_iso14443a_poller_trx_short_frame(NfcIso14443aShortFrame frame);

/** Transmit SDD frame */
NfcError nfc_iso14443a_tx_sdd_frame(const uint8_t* tx_data, size_t tx_bits);

/** Receive SDD frame */
NfcError nfc_iso14443a_rx_sdd_frame(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits);

/** Transmit with custom parity (poller) */
NfcError nfc_iso14443a_poller_tx_custom_parity(const uint8_t* tx_data, size_t tx_bits);

/** Set listener collision resolution data (UID/ATQA/SAK → PT Memory A) */
NfcError nfc_iso14443a_listener_set_col_res_data(
    uint8_t* uid, uint8_t uid_len, uint8_t* atqa, uint8_t sak);

/* ========================================================================== */
/*  FeliCa Specific Functions                                                 */
/* ========================================================================== */

/** Set SENSF_RES data for FeliCa listener (→ PT Memory F) */
NfcError nfc_felica_listener_set_sensf_res_data(
    const uint8_t* idm, uint8_t idm_len,
    const uint8_t* pmm, uint8_t pmm_len,
    uint16_t sys_code);

#ifdef __cplusplus
}
#endif
