/**
 * @file st25r3916_driver.c
 * @brief ST25R3916 NFC chip driver for ESP-IDF (ported from Flipper Zero).
 *
 * Complete implementation of register access, FIFO operations, IRQ management,
 * passive target memory access, test register support, and direct commands.
 *
 * The SPI handle is stored internally via st25r3916_set_spi().
 */

#include "st25r3916_driver.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ST25R3916"

/* ========================================================================== */
/*  Internal SPI Handle                                                       */
/* ========================================================================== */

static spi_device_handle_t s_spi = NULL;

void st25r3916_set_spi(spi_device_handle_t spi) {
    s_spi = spi;
}

/* ========================================================================== */
/*  Internal SPI Transaction Helper                                           */
/* ========================================================================== */

/**
 * @brief Perform a full-duplex SPI transaction.
 *
 * Sends tx_len bytes, then clocks rx_len more bytes to receive data.
 * Uses DMA-capable buffers for ESP-IDF SPI compatibility.
 */
static esp_err_t st25r3916_transaction(
    const uint8_t* tx_data,
    size_t tx_len,
    uint8_t* rx_data,
    size_t rx_len) {

    if (tx_len == 0 && rx_len == 0) return ESP_OK;

    size_t total_len = tx_len + rx_len;

    uint8_t* buffer_tx = heap_caps_malloc(total_len, MALLOC_CAP_DMA);
    uint8_t* buffer_rx = heap_caps_malloc(total_len, MALLOC_CAP_DMA);

    if (!buffer_tx || !buffer_rx) {
        if (buffer_tx) free(buffer_tx);
        if (buffer_rx) free(buffer_rx);
        return ESP_ERR_NO_MEM;
    }

    memcpy(buffer_tx, tx_data, tx_len);
    memset(buffer_tx + tx_len, 0x00, rx_len);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = total_len * 8;
    t.tx_buffer = buffer_tx;
    t.rx_buffer = buffer_rx;

    esp_err_t ret = spi_device_transmit(s_spi, &t);

    if (ret == ESP_OK && rx_data) {
        memcpy(rx_data, buffer_rx + tx_len, rx_len);
    }

    free(buffer_tx);
    free(buffer_rx);
    return ret;
}

/* ========================================================================== */
/*  Register Read / Write                                                     */
/* ========================================================================== */

esp_err_t st25r3916_read_burst_regs(
    uint8_t reg,
    uint8_t* values,
    uint8_t length) {

    uint8_t header[2];
    uint8_t header_len = 0;

    if (reg & ST25R3916_SPACE_B) {
        header[header_len++] = ST25R3916_CMD_SPACE_B_ACCESS;
    }
    header[header_len++] = (reg & ~ST25R3916_SPACE_B) | ST25R3916_READ_MODE;

    return st25r3916_transaction(header, header_len, values, length);
}

esp_err_t st25r3916_write_burst_regs(
    uint8_t reg,
    const uint8_t* values,
    uint8_t length) {

    uint8_t* tx_buf = heap_caps_malloc(2 + length, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    uint8_t idx = 0;
    if (reg & ST25R3916_SPACE_B) {
        tx_buf[idx++] = ST25R3916_CMD_SPACE_B_ACCESS;
    }
    tx_buf[idx++] = (reg & ~ST25R3916_SPACE_B) | ST25R3916_WRITE_MODE;
    memcpy(&tx_buf[idx], values, length);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (idx + length) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_read_reg(uint8_t reg, uint8_t* value) {
    return st25r3916_read_burst_regs(reg, value, 1);
}

esp_err_t st25r3916_write_reg(uint8_t reg, uint8_t value) {
    return st25r3916_write_burst_regs(reg, &value, 1);
}

/* ========================================================================== */
/*  Register Bit Manipulation                                                 */
/* ========================================================================== */

bool st25r3916_check_reg(uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t reg_val;
    st25r3916_read_reg(reg, &reg_val);
    return ((reg_val & mask) == val);
}

esp_err_t st25r3916_modify_reg(
    uint8_t reg,
    uint8_t clr_mask,
    uint8_t set_mask) {

    uint8_t reg_val;
    esp_err_t ret = st25r3916_read_reg(reg, &reg_val);
    if (ret != ESP_OK) return ret;

    uint8_t new_val = (reg_val & ~clr_mask) | set_mask;
    return st25r3916_write_reg(reg, new_val);
}

esp_err_t st25r3916_set_reg_bits(uint8_t reg, uint8_t set_mask) {
    uint8_t reg_val;
    esp_err_t ret = st25r3916_read_reg(reg, &reg_val);
    if (ret != ESP_OK) return ret;

    /* Skip write if bits already set */
    if ((reg_val & set_mask) == set_mask) {
        return ESP_OK;
    }

    return st25r3916_write_reg(reg, reg_val | set_mask);
}

esp_err_t st25r3916_clear_reg_bits(uint8_t reg, uint8_t clr_mask) {
    uint8_t reg_val;
    esp_err_t ret = st25r3916_read_reg(reg, &reg_val);
    if (ret != ESP_OK) return ret;

    /* Skip write if bits already clear */
    if ((reg_val & clr_mask) == 0) {
        return ESP_OK;
    }

    return st25r3916_write_reg(reg, reg_val & ~clr_mask);
}

esp_err_t st25r3916_change_reg_bits(
    uint8_t reg,
    uint8_t mask,
    uint8_t value) {
    return st25r3916_modify_reg(reg, mask, mask & value);
}

/* ========================================================================== */
/*  Test Register Access                                                      */
/* ========================================================================== */

esp_err_t st25r3916_read_test_reg(uint8_t reg, uint8_t* val) {
    uint8_t header[2] = {
        ST25R3916_CMD_TEST_ACCESS,
        reg | ST25R3916_READ_MODE
    };
    return st25r3916_transaction(header, 2, val, 1);
}

esp_err_t st25r3916_write_test_reg(uint8_t reg, uint8_t val) {
    uint8_t* tx_buf = heap_caps_malloc(3, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    tx_buf[0] = ST25R3916_CMD_TEST_ACCESS;
    tx_buf[1] = reg | ST25R3916_WRITE_MODE;
    tx_buf[2] = val;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 3 * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_change_test_reg_bits(
    uint8_t reg,
    uint8_t mask,
    uint8_t value) {

    uint8_t reg_val;
    esp_err_t ret = st25r3916_read_test_reg(reg, &reg_val);
    if (ret != ESP_OK) return ret;

    uint8_t new_val = (reg_val & ~mask) | (mask & value);
    return st25r3916_write_test_reg(reg, new_val);
}

/* ========================================================================== */
/*  FIFO Operations                                                           */
/* ========================================================================== */

esp_err_t st25r3916_reg_write_fifo(
    const uint8_t* buff,
    size_t length) {

    if (length == 0) return ESP_OK;

    uint8_t* tx_buf = heap_caps_malloc(1 + length, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    tx_buf[0] = ST25R3916_FIFO_LOAD;
    memcpy(&tx_buf[1], buff, length);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (1 + length) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_reg_read_fifo(
    uint8_t* buff,
    size_t length) {

    if (length == 0) return ESP_OK;

    uint8_t cmd = ST25R3916_FIFO_READ;
    return st25r3916_transaction(&cmd, 1, buff, length);
}

void st25r3916_write_fifo(
    const uint8_t* buff,
    size_t bits) {

    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES2, (uint8_t)(bits & 0xFFU));
    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES1, (uint8_t)((bits >> 8U) & 0xFFU));

    size_t bytes = (bits + 7) / 8;
    if (bytes > 0) {
        st25r3916_reg_write_fifo(buff, bytes);
    }
}

bool st25r3916_read_fifo(
    uint8_t* buff,
    size_t buff_size,
    size_t* buff_bits) {

    /* Read FIFO status registers to get actual byte count and last-byte bits */
    uint8_t fifo_status[ST25R3916_FIFO_STATUS_LEN] = {0};
    st25r3916_read_burst_regs(ST25R3916_REG_FIFO_STATUS1, fifo_status, 2);

    /* Byte count: FIFO_STATUS1 (full byte) + FIFO_STATUS2 bits 7:6 (upper 2 bits) */
    size_t bytes = fifo_status[0];
    bytes |= ((size_t)(fifo_status[1] & ST25R3916_REG_FIFO_STATUS2_fifo_b_mask)
              >> ST25R3916_REG_FIFO_STATUS2_fifo_b_shift) << 8;

    if (bytes == 0) {
        *buff_bits = 0;
        return false;
    }

    if (bytes > buff_size) {
        /* Buffer overflow — signal error but still report the size */
        *buff_bits = bytes * 8;
        return false;
    }

    /* Read the actual FIFO data */
    st25r3916_reg_read_fifo(buff, bytes);

    /* Calculate exact bit count */
    *buff_bits = bytes * 8;

    uint8_t last_byte_bits =
        (fifo_status[1] & ST25R3916_REG_FIFO_STATUS2_fifo_lb_mask)
        >> ST25R3916_REG_FIFO_STATUS2_fifo_lb_shift;
    bool last_byte_incomplete =
        (fifo_status[1] & ST25R3916_REG_FIFO_STATUS2_np_lb) != 0;

    if (last_byte_incomplete && last_byte_bits > 0) {
        *buff_bits -= (8 - last_byte_bits);
    }

    return true;
}

/* ========================================================================== */
/*  Direct Commands                                                           */
/* ========================================================================== */

esp_err_t st25r3916_direct_cmd(uint8_t cmd) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    /* Direct commands must be sent via DMA-capable memory */
    uint8_t* tx = heap_caps_malloc(1, MALLOC_CAP_DMA);
    if (!tx) return ESP_ERR_NO_MEM;

    *tx = cmd;
    t.length = 8;
    t.tx_buffer = tx;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx);
    return ret;
}

/* ========================================================================== */
/*  Passive Target Memory                                                     */
/* ========================================================================== */

esp_err_t st25r3916_write_pta_mem(
    const uint8_t* values,
    size_t length) {

    uint8_t* tx_buf = heap_caps_malloc(1 + length, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    tx_buf[0] = ST25R3916_PT_A_CONFIG_LOAD;
    memcpy(&tx_buf[1], values, length);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (1 + length) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_write_ptf_mem(
    const uint8_t* values,
    size_t length) {

    uint8_t* tx_buf = heap_caps_malloc(1 + length, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    tx_buf[0] = ST25R3916_PT_F_CONFIG_LOAD;
    memcpy(&tx_buf[1], values, length);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (1 + length) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_write_pttsn_mem(
    const uint8_t* values,
    size_t length) {

    uint8_t* tx_buf = heap_caps_malloc(1 + length, MALLOC_CAP_DMA);
    if (!tx_buf) return ESP_ERR_NO_MEM;

    tx_buf[0] = ST25R3916_PT_TSN_DATA_LOAD;
    memcpy(&tx_buf[1], values, length);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (1 + length) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi, &t);
    free(tx_buf);
    return ret;
}

esp_err_t st25r3916_read_pta_mem(
    uint8_t* buff,
    size_t length) {

    uint8_t cmd = ST25R3916_PT_MEM_READ;
    uint8_t* rx_buf = heap_caps_malloc(length + 1, MALLOC_CAP_DMA);
    if (!rx_buf) return ESP_ERR_NO_MEM;

    esp_err_t ret = st25r3916_transaction(&cmd, 1, rx_buf, length + 1);
    if (ret == ESP_OK) {
        memcpy(buff, &rx_buf[1], length);
    }

    free(rx_buf);
    return ret;
}

/* ========================================================================== */
/*  IRQ Management                                                            */
/* ========================================================================== */

void st25r3916_mask_irq(uint32_t mask) {
    uint8_t irq_mask_regs[4] = {
        (uint8_t)(mask & 0xFFU),
        (uint8_t)((mask >> 8) & 0xFFU),
        (uint8_t)((mask >> 16) & 0xFFU),
        (uint8_t)((mask >> 24) & 0xFFU),
    };
    st25r3916_write_burst_regs(ST25R3916_REG_IRQ_MASK_MAIN, irq_mask_regs, 4);
}

uint32_t st25r3916_get_irq(void) {
    uint8_t mask_regs[4] = {0};
    uint8_t irq_regs[4] = {0};

    st25r3916_read_burst_regs(ST25R3916_REG_IRQ_MASK_MAIN, mask_regs, 4);
    st25r3916_read_burst_regs(ST25R3916_REG_IRQ_MAIN, irq_regs, 4);

    uint32_t irq_status = 0;
    for (uint8_t i = 0; i < 4; i++) {
        irq_status |= (uint32_t)(irq_regs[i] & ~mask_regs[i]) << (i * 8);
    }

    return irq_status;
}
