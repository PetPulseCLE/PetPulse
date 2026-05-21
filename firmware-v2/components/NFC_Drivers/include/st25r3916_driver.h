/**
 * @file st25r3916_driver.h
 * @brief ST25R3916 NFC chip driver for ESP-IDF (ported from Flipper Zero).
 *
 * Provides low-level register access, FIFO operations, IRQ management,
 * passive target memory access, and test register support.
 *
 * The SPI handle is stored internally — call st25r3916_set_spi() once
 * during NFC init, then all subsequent functions use it automatically.
 */
#pragma once

#include "st25r3916_defs.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  IRQ Mask Definitions                                                      */
/* ========================================================================== */

#define ST25R3916_IRQ_MASK_ALL  ((uint32_t)0xFFFFFFFFUL)
#define ST25R3916_IRQ_MASK_NONE ((uint32_t)0x00000000UL)

/** Main interrupt register (byte 0) */
#define ST25R3916_IRQ_MASK_OSC      ((uint32_t)0x00000080U)
#define ST25R3916_IRQ_MASK_FWL      ((uint32_t)0x00000040U)
#define ST25R3916_IRQ_MASK_RXS      ((uint32_t)0x00000020U)
#define ST25R3916_IRQ_MASK_RXE      ((uint32_t)0x00000010U)
#define ST25R3916_IRQ_MASK_TXE      ((uint32_t)0x00000008U)
#define ST25R3916_IRQ_MASK_COL      ((uint32_t)0x00000004U)
#define ST25R3916_IRQ_MASK_RX_REST  ((uint32_t)0x00000002U)
#define ST25R3916_IRQ_MASK_RFU      ((uint32_t)0x00000001U)

/** Timer and NFC interrupt register (byte 1) */
#define ST25R3916_IRQ_MASK_DCT      ((uint32_t)0x00008000U)
#define ST25R3916_IRQ_MASK_NRE      ((uint32_t)0x00004000U)
#define ST25R3916_IRQ_MASK_GPE      ((uint32_t)0x00002000U)
#define ST25R3916_IRQ_MASK_EON      ((uint32_t)0x00001000U)
#define ST25R3916_IRQ_MASK_EOF      ((uint32_t)0x00000800U)
#define ST25R3916_IRQ_MASK_CAC      ((uint32_t)0x00000400U)
#define ST25R3916_IRQ_MASK_CAT      ((uint32_t)0x00000200U)
#define ST25R3916_IRQ_MASK_NFCT     ((uint32_t)0x00000100U)

/** Error and wake-up interrupt register (byte 2) */
#define ST25R3916_IRQ_MASK_CRC      ((uint32_t)0x00800000U)
#define ST25R3916_IRQ_MASK_PAR      ((uint32_t)0x00400000U)
#define ST25R3916_IRQ_MASK_ERR2     ((uint32_t)0x00200000U)
#define ST25R3916_IRQ_MASK_ERR1     ((uint32_t)0x00100000U)
#define ST25R3916_IRQ_MASK_WT       ((uint32_t)0x00080000U)
#define ST25R3916_IRQ_MASK_WAM      ((uint32_t)0x00040000U)
#define ST25R3916_IRQ_MASK_WPH      ((uint32_t)0x00020000U)
#define ST25R3916_IRQ_MASK_WCAP     ((uint32_t)0x00010000U)

/** Passive target interrupt register (byte 3) */
#define ST25R3916_IRQ_MASK_PPON2    ((uint32_t)0x80000000U)
#define ST25R3916_IRQ_MASK_SL_WL    ((uint32_t)0x40000000U)
#define ST25R3916_IRQ_MASK_APON     ((uint32_t)0x20000000U)
#define ST25R3916_IRQ_MASK_RXE_PTA  ((uint32_t)0x10000000U)
#define ST25R3916_IRQ_MASK_WU_F     ((uint32_t)0x08000000U)
#define ST25R3916_IRQ_MASK_RFU2     ((uint32_t)0x04000000U)
#define ST25R3916_IRQ_MASK_WU_A_X   ((uint32_t)0x02000000U)
#define ST25R3916_IRQ_MASK_WU_A     ((uint32_t)0x01000000U)

/* ========================================================================== */
/*  SPI Handle Management                                                     */
/* ========================================================================== */

/** Set the SPI handle (call once after SPI device is added to bus) */
void st25r3916_set_spi(spi_device_handle_t spi);

/* ========================================================================== */
/*  Register Access Functions                                                 */
/* ========================================================================== */

/** Read a single register */
esp_err_t st25r3916_read_reg(uint8_t reg, uint8_t* value);

/** Write a single register */
esp_err_t st25r3916_write_reg(uint8_t reg, uint8_t value);

/** Read multiple consecutive registers */
esp_err_t st25r3916_read_burst_regs(uint8_t reg, uint8_t* values, uint8_t length);

/** Write multiple consecutive registers */
esp_err_t st25r3916_write_burst_regs(uint8_t reg, const uint8_t* values, uint8_t length);

/** Check if register bits match expected value. Returns true on match. */
bool st25r3916_check_reg(uint8_t reg, uint8_t mask, uint8_t val);

/** Modify register: clear clr_mask bits, then set set_mask bits */
esp_err_t st25r3916_modify_reg(uint8_t reg, uint8_t clr_mask, uint8_t set_mask);

/** Set register bits (read-modify-write, skip write if already set) */
esp_err_t st25r3916_set_reg_bits(uint8_t reg, uint8_t set_mask);

/** Clear register bits (read-modify-write, skip write if already clear) */
esp_err_t st25r3916_clear_reg_bits(uint8_t reg, uint8_t clr_mask);

/** Change register bits: apply value within mask (equivalent to modify_reg with mask, mask & value) */
esp_err_t st25r3916_change_reg_bits(uint8_t reg, uint8_t mask, uint8_t value);

/* ========================================================================== */
/*  Test Register Access                                                      */
/* ========================================================================== */

/** Read a test register */
esp_err_t st25r3916_read_test_reg(uint8_t reg, uint8_t* val);

/** Write a test register */
esp_err_t st25r3916_write_test_reg(uint8_t reg, uint8_t val);

/** Change test register bits: apply value within mask */
esp_err_t st25r3916_change_test_reg_bits(uint8_t reg, uint8_t mask, uint8_t value);

/* ========================================================================== */
/*  FIFO Operations                                                           */
/* ========================================================================== */

/**
 * @brief Write data to FIFO and set NUM_TX_BYTES registers.
 * @param buff Data buffer
 * @param bits Number of bits to transmit (registers store bit count)
 */
void st25r3916_write_fifo(const uint8_t* buff, size_t bits);

/**
 * @brief Read data from FIFO, checking FIFO_STATUS registers for actual count.
 * @param buff Output buffer
 * @param buff_size Maximum buffer size in bytes
 * @param buff_bits Output: actual number of bits read
 * @return true on success, false on empty FIFO or buffer overflow
 */
bool st25r3916_read_fifo(uint8_t* buff, size_t buff_size, size_t* buff_bits);

/** Low-level FIFO write (raw, no NUM_TX_BYTES) */
esp_err_t st25r3916_reg_write_fifo(const uint8_t* buff, size_t length);

/** Low-level FIFO read (raw, no status check) */
esp_err_t st25r3916_reg_read_fifo(uint8_t* buff, size_t length);

/* ========================================================================== */
/*  Direct Commands                                                           */
/* ========================================================================== */

/** Send a direct command to the ST25R3916 */
esp_err_t st25r3916_direct_cmd(uint8_t cmd);

/* ========================================================================== */
/*  Passive Target Memory                                                     */
/* ========================================================================== */

/** Write Passive Target Memory A (ISO14443A collision resolution data) */
esp_err_t st25r3916_write_pta_mem(const uint8_t* values, size_t length);

/** Read Passive Target Memory */
esp_err_t st25r3916_read_pta_mem(uint8_t* buff, size_t length);

/** Write Passive Target Memory F (FeliCa response data) */
esp_err_t st25r3916_write_ptf_mem(const uint8_t* values, size_t length);

/** Write Passive Target TSN data */
esp_err_t st25r3916_write_pttsn_mem(const uint8_t* values, size_t length);

/* ========================================================================== */
/*  IRQ Management                                                            */
/* ========================================================================== */

/**
 * @brief Write IRQ mask registers (4 bytes).
 *        Bits set to 1 are MASKED (disabled).
 * @param mask 32-bit mask value
 */
void st25r3916_mask_irq(uint32_t mask);

/**
 * @brief Read IRQ status registers (clears on read).
 *        Also reads mask registers first (Flipper behavior).
 * @return 32-bit IRQ status bitmask
 */
uint32_t st25r3916_get_irq(void);

#ifdef __cplusplus
}
#endif
