/**
 * @file nfc_driver.c
 * @brief NFC HAL driver for ESP-IDF with ST25R3916 (ported from Flipper Zero).
 *
 * Complete implementation: full init sequence, GPIO ISR, EventGroup-based event
 * system, esp_timer-based FWT/BlockTx timers, mode/tech dispatch, field control,
 * poller/listener common functions.
 *
 * All SPI access goes through the BSP + st25r3916 driver internally —
 * no SPI handles are passed through the public API.
 */

#include "nfc_driver.h"
#include "st25r3916_driver.h"
#include "bsp.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#define TAG "NFC"

/** NFC carrier frequency in Hz (13.56 MHz) */
#define NFC_CARRIER_HZ (13560000UL)

/* ========================================================================== */
/*  Global State                                                              */
/* ========================================================================== */

NfcState nfc_state;

/** Tech dispatch table */
const NfcTechBase* const nfc_tech_table[NfcTechNum] = {
    [NfcTechIso14443a] = &nfc_tech_iso14443a,
    [NfcTechIso14443b] = &nfc_tech_iso14443b,
    [NfcTechIso15693]  = &nfc_tech_iso15693,
    [NfcTechFelica]    = &nfc_tech_felica,
};

/* ========================================================================== */
/*  Static Variables                                                          */
/* ========================================================================== */

static spi_device_handle_t nfc_spi = NULL;
static SemaphoreHandle_t nfc_irq_sem = NULL;
static EventGroupHandle_t nfc_event_group = NULL;

static esp_timer_handle_t nfc_timer_fwt = NULL;
static esp_timer_handle_t nfc_timer_block_tx = NULL;

static bool nfc_gpio_isr_installed = false;
static bool nfc_timers_initialized = false;

/* ========================================================================== */
/*  GPIO ISR & IRQ Semaphore                                                  */
/* ========================================================================== */

/**
 * GPIO ISR handler: signals both the EventGroup and the binary semaphore.
 */
static void IRAM_ATTR nfc_isr_handler(void* arg) {
    (void)arg;
    BaseType_t higher_woken = pdFALSE;

    /* Signal EventGroup for the event system */
    if (nfc_event_group) {
        xEventGroupSetBitsFromISR(nfc_event_group, NfcEventInternalIrq, &higher_woken);
    }

    /* Also signal semaphore for legacy wait_for_irq */
    if (nfc_irq_sem) {
        xSemaphoreGiveFromISR(nfc_irq_sem, &higher_woken);
    }

    if (higher_woken) {
        portYIELD_FROM_ISR();
    }
}

void nfc_init_gpio_isr(void) {
    if (nfc_gpio_isr_installed) return;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BSP_INT_NFC),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);

    /* ISR service is installed by BSP — just add our handler */
    gpio_isr_handler_add(BSP_INT_NFC, nfc_isr_handler, NULL);
    nfc_gpio_isr_installed = true;
}

void nfc_deinit_gpio_isr(void) {
    if (!nfc_gpio_isr_installed) return;

    gpio_isr_handler_remove(BSP_INT_NFC);
    gpio_set_intr_type(BSP_INT_NFC, GPIO_INTR_DISABLE);
    nfc_gpio_isr_installed = false;
}

uint32_t nfc_get_irq(void) {
    /*
     * loop while GPIO is high to drain all pending IRQs.
     * This ensures no IRQ edges are missed when multiple fire simultaneously.
     */
    uint32_t irq = 0;
    while (gpio_get_level(BSP_INT_NFC)) {
        irq |= st25r3916_get_irq();
    }
    return irq;
}

esp_err_t nfc_wait_for_irq(uint32_t timeout_ms) {
    if (!nfc_irq_sem) return ESP_ERR_INVALID_STATE;

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(nfc_irq_sem, ticks) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

/* ========================================================================== */
/*  Event System (FreeRTOS EventGroups)                                       */
/* ========================================================================== */

void nfc_event_init(void) {
    if (!nfc_event_group) {
        nfc_event_group = xEventGroupCreate();
    }
}

NfcError nfc_event_start(void) {
    if (!nfc_event_group) return NfcErrorInternal;
    xEventGroupClearBits(nfc_event_group, NFC_EVENT_INTERNAL_ALL);
    return NfcErrorNone;
}

NfcError nfc_event_stop(void) {
    if (!nfc_event_group) return NfcErrorInternal;
    xEventGroupClearBits(nfc_event_group, NFC_EVENT_INTERNAL_ALL);
    return NfcErrorNone;
}

void nfc_event_set(NfcEventInternalType event) {
    if (nfc_event_group) {
        xEventGroupSetBits(nfc_event_group, event);
    }
}

void nfc_event_set_isr(NfcEventInternalType event) {
    if (nfc_event_group) {
        BaseType_t higher_woken = pdFALSE;
        xEventGroupSetBitsFromISR(nfc_event_group, event, &higher_woken);
        if (higher_woken) {
            portYIELD_FROM_ISR();
        }
    }
}

NfcError nfc_abort(void) {
    nfc_event_set(NfcEventInternalAbort);
    return NfcErrorNone;
}

NfcEvent nfc_wait_event_common(uint32_t timeout_ms) {
    if (!nfc_event_group) return NFC_EVENT_TIMEOUT;

    NfcEvent event = NFC_EVENT_NONE;
    TickType_t wait_ticks = (timeout_ms == NFC_EVENT_WAIT_FOREVER) ?
                            portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        nfc_event_group,
        NFC_EVENT_INTERNAL_ALL,
        pdFALSE, /* Don't auto-clear */
        pdFALSE, /* Wait for ANY bit */
        wait_ticks);

    if (bits == 0) {
        return NFC_EVENT_TIMEOUT;
    }

    if (bits & NfcEventInternalIrq) {
        xEventGroupClearBits(nfc_event_group, NfcEventInternalIrq);
        uint32_t irq = nfc_get_irq();

        if (irq & ST25R3916_IRQ_MASK_OSC)
            event |= NFC_EVENT_OSC_ON;
        if (irq & ST25R3916_IRQ_MASK_TXE)
            event |= NFC_EVENT_TX_END;
        if (irq & ST25R3916_IRQ_MASK_RXS)
            event |= NFC_EVENT_RX_START;
        if (irq & ST25R3916_IRQ_MASK_RXE)
            event |= NFC_EVENT_RX_END;
        if (irq & ST25R3916_IRQ_MASK_COL)
            event |= NFC_EVENT_COLLISION;
        if (irq & ST25R3916_IRQ_MASK_EON)
            event |= NFC_EVENT_FIELD_ON;
        if (irq & ST25R3916_IRQ_MASK_EOF)
            event |= NFC_EVENT_FIELD_OFF;
        if (irq & (ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X | ST25R3916_IRQ_MASK_WU_F))
            event |= NFC_EVENT_LISTENER_ACTIVE;
    }

    if (bits & NfcEventInternalTimerFwtExpired) {
        event |= NFC_EVENT_TIMER_FWT_EXPIRED;
        xEventGroupClearBits(nfc_event_group, NfcEventInternalTimerFwtExpired);
    }
    if (bits & NfcEventInternalTimerBlockTxExpired) {
        event |= NFC_EVENT_TIMER_BLOCK_TX_EXPIRED;
        xEventGroupClearBits(nfc_event_group, NfcEventInternalTimerBlockTxExpired);
    }
    if (bits & NfcEventInternalAbort) {
        event |= NFC_EVENT_ABORT_REQUEST;
        xEventGroupClearBits(nfc_event_group, NfcEventInternalAbort);
    }

    return event;
}

bool nfc_event_wait_for_specific_irq(uint32_t mask, uint32_t timeout_ms) {
    if (!nfc_event_group) return false;

    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(
        nfc_event_group, NfcEventInternalIrq, pdTRUE, pdFALSE, ticks);

    if (bits & NfcEventInternalIrq) {
        uint32_t irq = nfc_get_irq();
        return ((irq & mask) == mask);
    }

    return false;
}

/* ========================================================================== */
/*  Timer System (esp_timer)                                                  */
/* ========================================================================== */

static void nfc_timer_fwt_callback(void* arg) {
    (void)arg;
    nfc_event_set(NfcEventInternalTimerFwtExpired);
}

static void nfc_timer_block_tx_callback(void* arg) {
    (void)arg;
    nfc_event_set(NfcEventInternalTimerBlockTxExpired);
}

void nfc_timers_init(void) {
    if (nfc_timers_initialized) return;

    esp_timer_create_args_t fwt_args = {
        .callback = nfc_timer_fwt_callback,
        .name = "nfc_fwt",
    };
    esp_timer_create(&fwt_args, &nfc_timer_fwt);

    esp_timer_create_args_t btx_args = {
        .callback = nfc_timer_block_tx_callback,
        .name = "nfc_btx",
    };
    esp_timer_create(&btx_args, &nfc_timer_block_tx);

    nfc_timers_initialized = true;
}

void nfc_timers_deinit(void) {
    if (!nfc_timers_initialized) return;

    if (nfc_timer_fwt) {
        esp_timer_stop(nfc_timer_fwt);
        esp_timer_delete(nfc_timer_fwt);
        nfc_timer_fwt = NULL;
    }
    if (nfc_timer_block_tx) {
        esp_timer_stop(nfc_timer_block_tx);
        esp_timer_delete(nfc_timer_block_tx);
        nfc_timer_block_tx = NULL;
    }

    nfc_timers_initialized = false;
}

/**
 * Get compensation value for a timer based on current mode/tech.
 */
static int32_t nfc_timer_get_compensation(bool is_fwt) {
    const NfcTechBase* tech = nfc_tech_table[nfc_state.tech];

    if (nfc_state.mode == NfcModePoller) {
        if (is_fwt)
            return tech->poller.compensation.fwt;
        else
            return tech->poller.compensation.fdt;
    } else if (nfc_state.mode == NfcModeListener) {
        return tech->listener.compensation.fdt;
    }

    return 0;
}

/**
 * Start a timer with carrier-cycle delay, applying compensation.
 */
static void nfc_timer_start_fc(esp_timer_handle_t timer, uint32_t time_fc, bool is_fwt) {
    int32_t comp = nfc_timer_get_compensation(is_fwt);

    /* Don't start if compensation exceeds the requested delay */
    if (comp >= (int32_t)time_fc) return;

    /* Convert carrier cycles to microseconds: us = (fc - comp) / NFC_CARRIER_HZ * 1e6 */
    uint64_t fc_adj = (uint64_t)(time_fc - comp);
    uint64_t us = (fc_adj * 1000000ULL) / NFC_CARRIER_HZ;
    if (us == 0) us = 1;

    esp_timer_stop(timer); /* Stop if already running */
    esp_timer_start_once(timer, us);
}

void nfc_timer_fwt_start(uint32_t time_fc) {
    if (nfc_timer_fwt) {
        nfc_timer_start_fc(nfc_timer_fwt, time_fc, true);
    }
}

void nfc_timer_fwt_stop(void) {
    if (nfc_timer_fwt) {
        esp_timer_stop(nfc_timer_fwt);
    }
}

void nfc_timer_block_tx_start(uint32_t time_fc) {
    if (nfc_timer_block_tx) {
        nfc_timer_start_fc(nfc_timer_block_tx, time_fc, false);
    }
}

void nfc_timer_block_tx_start_us(uint32_t time_us) {
    if (nfc_timer_block_tx) {
        esp_timer_stop(nfc_timer_block_tx);
        esp_timer_start_once(nfc_timer_block_tx, time_us);
    }
}

void nfc_timer_block_tx_stop(void) {
    if (nfc_timer_block_tx) {
        esp_timer_stop(nfc_timer_block_tx);
    }
}

bool nfc_timer_block_tx_is_running(void) {
    if (nfc_timer_block_tx) {
        return esp_timer_is_active(nfc_timer_block_tx);
    }
    return false;
}

/* ========================================================================== */
/*  SPI Initialization                                                        */
/* ========================================================================== */

static esp_err_t nfc_spi_init(void) {
    if (nfc_spi) return ESP_OK;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = BSP_CS_NFC,
        .queue_size = 7,
    };

    esp_err_t ret = spi_bus_add_device(BSP_SPI1_HOST, &devcfg, &nfc_spi);
    if (ret == ESP_OK) {
        /* Share the SPI handle with the ST25R3916 driver */
        st25r3916_set_spi(nfc_spi);
    }
    return ret;
}

/* ========================================================================== */
/*  Turn On Oscillator                                                        */
/* ========================================================================== */

static NfcError nfc_turn_on_osc(void) {
    NfcError error = NfcErrorNone;
    nfc_event_start();

    if (!st25r3916_check_reg(
            ST25R3916_REG_OP_CONTROL,
            ST25R3916_REG_OP_CONTROL_en,
            ST25R3916_REG_OP_CONTROL_en)) {
        /* Unmask OSC IRQ, enable chip */
        st25r3916_mask_irq(~ST25R3916_IRQ_MASK_OSC);
        st25r3916_set_reg_bits(ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_en);
        nfc_event_wait_for_specific_irq(ST25R3916_IRQ_MASK_OSC, 10);
    }

    /* Mask all IRQs */
    st25r3916_mask_irq(ST25R3916_IRQ_MASK_ALL);

    /* Verify oscillator is running */
    bool osc_on = st25r3916_check_reg(
        ST25R3916_REG_AUX_DISPLAY,
        ST25R3916_REG_AUX_DISPLAY_osc_ok,
        ST25R3916_REG_AUX_DISPLAY_osc_ok);
    if (!osc_on) {
        error = NfcErrorOscillator;
    }

    return error;
}

/* ========================================================================== */
/*  Full Init Sequence */
/* ========================================================================== */

esp_err_t nfc_init(void) {
    ESP_LOGI(TAG, "Initializing NFC Driver...");

    NfcError error = NfcErrorNone;

    /* Create synchronization primitives */
    if (!nfc_irq_sem) {
        nfc_irq_sem = xSemaphoreCreateBinary();
    }

    /* Initialize event system */
    nfc_event_init();
    nfc_event_start();

    /* SPI Init — also sets the handle in st25r3916 driver */
    ESP_RETURN_ON_ERROR(nfc_spi_init(), TAG, "SPI Init Failed");

    /* ---- Full init sequence ---- */

    /* Set chip to default state */
    st25r3916_direct_cmd(ST25R3916_CMD_SET_DEFAULT);

    /* Increase IO driver strength of MISO and IRQ */
    st25r3916_write_reg(ST25R3916_REG_IO_CONF2,
                        ST25R3916_REG_IO_CONF2_io_drv_lvl);

    /* Check chip ID */
    uint8_t chip_id = 0;
    st25r3916_read_reg(ST25R3916_REG_IC_IDENTITY, &chip_id);
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    if ((chip_id & ST25R3916_REG_IC_IDENTITY_ic_type_mask) !=
        ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916) {
        ESP_LOGE(TAG, "Wrong chip ID!");
        return ESP_FAIL;
    }

    /* Clear IRQ status */
    st25r3916_get_irq();

    /* Mask all interrupts */
    st25r3916_mask_irq(ST25R3916_IRQ_MASK_ALL);

    /* Enable GPIO ISR for NFC IRQ pin */
    nfc_init_gpio_isr();

    /* Disable internal overheat protection (test register 0x04, bit 0x10) */
    st25r3916_change_test_reg_bits(0x04, 0x10, 0x10);

    /* Turn on oscillator */
    error = nfc_turn_on_osc();
    if (error != NfcErrorNone) {
        ESP_LOGE(TAG, "Oscillator failed to start");
        nfc_low_power_mode_start();
        return ESP_FAIL;
    }

    /* ---- Measure VDD ---- */
    st25r3916_change_reg_bits(
        ST25R3916_REG_REGULATOR_CONTROL,
        ST25R3916_REG_REGULATOR_CONTROL_mpsv_mask,
        ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd);
    st25r3916_mask_irq(~ST25R3916_IRQ_MASK_DCT);
    st25r3916_direct_cmd(ST25R3916_CMD_MEASURE_VDD);
    nfc_event_wait_for_specific_irq(ST25R3916_IRQ_MASK_DCT, 100);
    st25r3916_mask_irq(ST25R3916_IRQ_MASK_ALL);

    uint8_t ad_res = 0;
    st25r3916_read_reg(ST25R3916_REG_AD_RESULT, &ad_res);
    uint16_t mV = ((uint16_t)ad_res) * 23U;
    mV += (((((uint16_t)ad_res) * 4U) + 5U) / 10U);
    ESP_LOGI(TAG, "VDD: %u mV", mV);

    /* Select voltage threshold */
    if (mV < 3600) {
        st25r3916_change_reg_bits(
            ST25R3916_REG_IO_CONF2,
            ST25R3916_REG_IO_CONF2_sup3V,
            ST25R3916_REG_IO_CONF2_sup3V_3V);
    } else {
        st25r3916_change_reg_bits(
            ST25R3916_REG_IO_CONF2,
            ST25R3916_REG_IO_CONF2_sup3V,
            ST25R3916_REG_IO_CONF2_sup3V_5V);
    }

    /* Disable MCU CLK output */
    st25r3916_change_reg_bits(
        ST25R3916_REG_IO_CONF1,
        ST25R3916_REG_IO_CONF1_out_cl_mask | ST25R3916_REG_IO_CONF1_lf_clk_off,
        0x07);

    /* Disable MISO pull-down */
    st25r3916_change_reg_bits(
        ST25R3916_REG_IO_CONF2,
        ST25R3916_REG_IO_CONF2_miso_pd1 | ST25R3916_REG_IO_CONF2_miso_pd2,
        0x00);

    /* Set TX driver resistance to minimum (0) */
    st25r3916_change_reg_bits(
        ST25R3916_REG_TX_DRIVER,
        ST25R3916_REG_TX_DRIVER_d_res_mask,
        0x00);

    /* Use minimum non-overlap */
    st25r3916_change_reg_bits(
        ST25R3916_REG_RES_AM_MOD,
        ST25R3916_REG_RES_AM_MOD_fa3_f,
        ST25R3916_REG_RES_AM_MOD_fa3_f);

    /* Set field activation thresholds */
    st25r3916_change_reg_bits(
        ST25R3916_REG_FIELD_THRESHOLD_ACTV,
        ST25R3916_REG_FIELD_THRESHOLD_ACTV_trg_mask,
        ST25R3916_REG_FIELD_THRESHOLD_ACTV_trg_105mV);
    st25r3916_change_reg_bits(
        ST25R3916_REG_FIELD_THRESHOLD_ACTV,
        ST25R3916_REG_FIELD_THRESHOLD_ACTV_rfe_mask,
        ST25R3916_REG_FIELD_THRESHOLD_ACTV_rfe_105mV);

    /* Set field deactivation thresholds */
    st25r3916_change_reg_bits(
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV,
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV_trg_mask,
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV_trg_75mV);
    st25r3916_change_reg_bits(
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV,
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV_rfe_mask,
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV_rfe_75mV);

    /* Enable external load modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_AUX_MOD_lm_ext,
        ST25R3916_REG_AUX_MOD_lm_ext);

    /* Enable internal load modulation */
    st25r3916_change_reg_bits(
        ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_AUX_MOD_lm_dri,
        ST25R3916_REG_AUX_MOD_lm_dri);

    /* Adjust FDT (frame delay time) */
    st25r3916_change_reg_bits(
        ST25R3916_REG_PASSIVE_TARGET,
        ST25R3916_REG_PASSIVE_TARGET_fdel_mask,
        (5U << ST25R3916_REG_PASSIVE_TARGET_fdel_shift));

    /* Reduce RFO resistance in modulated state */
    st25r3916_change_reg_bits(
        ST25R3916_REG_PT_MOD,
        ST25R3916_REG_PT_MOD_ptm_res_mask | ST25R3916_REG_PT_MOD_pt_res_mask,
        0x0f);

    /* Enable RX start on first 4 bits (EMV) */
    st25r3916_change_reg_bits(
        ST25R3916_REG_EMD_SUP_CONF,
        ST25R3916_REG_EMD_SUP_CONF_rx_start_emv,
        ST25R3916_REG_EMD_SUP_CONF_rx_start_emv_on);

    /* Set antenna tuning */
    st25r3916_change_reg_bits(ST25R3916_REG_ANT_TUNE_A, 0xFF, 0x82);
    st25r3916_change_reg_bits(ST25R3916_REG_ANT_TUNE_B, 0xFF, 0x82);

    /* Enable auto external field detection */
    st25r3916_change_reg_bits(
        ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en_fd_mask,
        ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

    /* Perform regulator calibration if needed */
    if (st25r3916_check_reg(
            ST25R3916_REG_REGULATOR_CONTROL,
            ST25R3916_REG_REGULATOR_CONTROL_reg_s,
            0x00)) {
        ESP_LOGI(TAG, "Adjusting regulators");
        st25r3916_set_reg_bits(
            ST25R3916_REG_REGULATOR_CONTROL,
            ST25R3916_REG_REGULATOR_CONTROL_reg_s);
        st25r3916_clear_reg_bits(
            ST25R3916_REG_REGULATOR_CONTROL,
            ST25R3916_REG_REGULATOR_CONTROL_reg_s);
        st25r3916_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
        vTaskDelay(pdMS_TO_TICKS(6));
    }

    /* Enter low power mode after init */
    nfc_low_power_mode_start();

    ESP_LOGI(TAG, "NFC Driver Initialized Successfully");
    return ESP_OK;
}

void nfc_deinit(void) {
    nfc_low_power_mode_start();

    if (nfc_spi) {
        spi_bus_remove_device(nfc_spi);
        st25r3916_set_spi(NULL);
        nfc_spi = NULL;
    }
    if (nfc_irq_sem) {
        vSemaphoreDelete(nfc_irq_sem);
        nfc_irq_sem = NULL;
    }
    if (nfc_event_group) {
        vEventGroupDelete(nfc_event_group);
        nfc_event_group = NULL;
    }
}

NfcError nfc_is_hal_ready(void) {
    uint8_t chip_id = 0;
    st25r3916_read_reg(ST25R3916_REG_IC_IDENTITY, &chip_id);
    if ((chip_id & ST25R3916_REG_IC_IDENTITY_ic_type_mask) !=
        ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916) {
        ESP_LOGE(TAG, "Wrong chip ID");
        return NfcErrorCommunication;
    }
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Power Management                                                          */
/* ========================================================================== */

NfcError nfc_low_power_mode_start(void) {
    st25r3916_direct_cmd(ST25R3916_CMD_STOP);
    st25r3916_clear_reg_bits(
        ST25R3916_REG_OP_CONTROL,
        (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
         ST25R3916_REG_OP_CONTROL_wu | ST25R3916_REG_OP_CONTROL_tx_en |
         ST25R3916_REG_OP_CONTROL_en_fd_mask));
    nfc_deinit_gpio_isr();
    nfc_timers_deinit();
    nfc_event_stop();
    return NfcErrorNone;
}

NfcError nfc_low_power_mode_stop(void) {
    NfcError error = NfcErrorNone;

    nfc_init_gpio_isr();
    nfc_timers_init();

    error = nfc_turn_on_osc();
    if (error != NfcErrorNone) return error;

    st25r3916_change_reg_bits(
        ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en_fd_mask,
        ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

    return NfcErrorNone;
}

/* ========================================================================== */
/*  Mode / Tech Dispatch                                                      */
/* ========================================================================== */

static NfcError nfc_poller_init_common(void) {
    /* Disable wake up */
    st25r3916_clear_reg_bits(ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_wu);

    /* Enable correlator */
    st25r3916_change_reg_bits(
        ST25R3916_REG_AUX,
        ST25R3916_REG_AUX_dis_corr,
        ST25R3916_REG_AUX_dis_corr_correlator);

    /* Set antenna tuning */
    st25r3916_change_reg_bits(ST25R3916_REG_ANT_TUNE_A, 0xFF, 0x82);
    st25r3916_change_reg_bits(ST25R3916_REG_ANT_TUNE_B, 0xFF, 0x82);

    /* Clear over/undershoot protection */
    st25r3916_write_reg(ST25R3916_REG_OVERSHOOT_CONF1, 0x00);
    st25r3916_write_reg(ST25R3916_REG_OVERSHOOT_CONF2, 0x00);
    st25r3916_write_reg(ST25R3916_REG_UNDERSHOOT_CONF1, 0x00);
    st25r3916_write_reg(ST25R3916_REG_UNDERSHOOT_CONF2, 0x00);

    return NfcErrorNone;
}

NfcError nfc_set_mode(NfcMode mode, NfcTech tech) {
    NfcError error = NfcErrorNone;

    if (mode == NfcModePoller) {
        error = nfc_poller_init_common();
        if (error != NfcErrorNone) return error;
        error = nfc_tech_table[tech]->poller.init();
    } else if (mode == NfcModeListener) {
        error = nfc_tech_table[tech]->listener.init();
    }

    nfc_state.mode = mode;
    nfc_state.tech = tech;
    return error;
}

NfcError nfc_reset_mode(void) {
    NfcError error = NfcErrorNone;

    st25r3916_direct_cmd(ST25R3916_CMD_STOP);

    if (nfc_state.mode == NfcModePoller) {
        error = nfc_tech_table[nfc_state.tech]->poller.deinit();
    } else if (nfc_state.mode == NfcModeListener) {
        error = nfc_tech_table[nfc_state.tech]->listener.deinit();
    }

    /* Restore default register values */
    st25r3916_write_reg(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om0);
    st25r3916_write_reg(ST25R3916_REG_STREAM_MODE, 0);
    st25r3916_clear_reg_bits(ST25R3916_REG_AUX, ST25R3916_REG_AUX_no_crc_rx);
    st25r3916_clear_reg_bits(ST25R3916_REG_BIT_RATE,
        ST25R3916_REG_BIT_RATE_txrate_mask | ST25R3916_REG_BIT_RATE_rxrate_mask);

    st25r3916_write_reg(ST25R3916_REG_RX_CONF1, 0);
    st25r3916_write_reg(ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF2_sqm_dyn | ST25R3916_REG_RX_CONF2_agc_en |
        ST25R3916_REG_RX_CONF2_agc_m);

    st25r3916_write_reg(ST25R3916_REG_CORR_CONF1,
        ST25R3916_REG_CORR_CONF1_corr_s7 | ST25R3916_REG_CORR_CONF1_corr_s4 |
        ST25R3916_REG_CORR_CONF1_corr_s1 | ST25R3916_REG_CORR_CONF1_corr_s0);
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, 0);

    return error;
}

/* ========================================================================== */
/*  Field Control                                                             */
/* ========================================================================== */

NfcError nfc_field_detect_start(void) {
    st25r3916_write_reg(ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_en_fd_mask);
    st25r3916_write_reg(ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_targ | ST25R3916_REG_MODE_om0);
    return NfcErrorNone;
}

NfcError nfc_field_detect_stop(void) {
    st25r3916_clear_reg_bits(ST25R3916_REG_OP_CONTROL,
        (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_en_fd_mask));
    return NfcErrorNone;
}

bool nfc_field_is_present(void) {
    return st25r3916_check_reg(
        ST25R3916_REG_AUX_DISPLAY,
        ST25R3916_REG_AUX_DISPLAY_efd_o,
        ST25R3916_REG_AUX_DISPLAY_efd_o);
}

NfcError nfc_poller_field_on(void) {
    if (!st25r3916_check_reg(
            ST25R3916_REG_OP_CONTROL,
            ST25R3916_REG_OP_CONTROL_tx_en,
            ST25R3916_REG_OP_CONTROL_tx_en)) {
        /* Set min guard time */
        st25r3916_write_reg(ST25R3916_REG_FIELD_ON_GT, 0);
        /* Enable TX + RX */
        st25r3916_set_reg_bits(ST25R3916_REG_OP_CONTROL,
            (ST25R3916_REG_OP_CONTROL_rx_en | ST25R3916_REG_OP_CONTROL_tx_en));
    }
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Common FIFO Helpers                                                       */
/* ========================================================================== */

NfcError nfc_common_fifo_tx(const uint8_t* tx_data, size_t tx_bits) {
    st25r3916_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_write_fifo(tx_data, tx_bits);
    st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
    return NfcErrorNone;
}

NfcError nfc_common_fifo_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    if (!st25r3916_read_fifo(rx_data, rx_data_size, rx_bits)) {
        return NfcErrorBufferOverflow;
    }
    return NfcErrorNone;
}

NfcError nfc_common_listener_rx_start(void) {
    st25r3916_direct_cmd(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
    return NfcErrorNone;
}

/* ========================================================================== */
/*  Poller TX/RX                                                              */
/* ========================================================================== */

NfcError nfc_poller_tx_common(const uint8_t* tx_data, size_t tx_bits) {
    /* Prepare TX */
    st25r3916_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_clear_reg_bits(
        ST25R3916_REG_TIMER_EMV_CONTROL,
        ST25R3916_REG_TIMER_EMV_CONTROL_nrt_emv);

    /* Ensure normal parity mode */
    st25r3916_change_reg_bits(
        ST25R3916_REG_ISO14443A_NFC,
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par),
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par_off | ST25R3916_REG_ISO14443A_NFC_no_rx_par_off));

    /* Setup IRQs */
    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE);
    st25r3916_get_irq();           /* Clear pending */
    st25r3916_mask_irq(~interrupts); /* Enable selected */

    st25r3916_write_fifo(tx_data, tx_bits);
    st25r3916_direct_cmd(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);

    return NfcErrorNone;
}

NfcError nfc_poller_tx(const uint8_t* tx_data, size_t tx_bits) {
    return nfc_tech_table[nfc_state.tech]->poller.tx(tx_data, tx_bits);
}

NfcError nfc_poller_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    return nfc_tech_table[nfc_state.tech]->poller.rx(rx_data, rx_data_size, rx_bits);
}

NfcEvent nfc_poller_wait_event(uint32_t timeout_ms) {
    return nfc_tech_table[nfc_state.tech]->poller.wait_event(timeout_ms);
}

/* ========================================================================== */
/*  Listener TX/RX                                                            */
/* ========================================================================== */

NfcEvent nfc_listener_wait_event(uint32_t timeout_ms) {
    return nfc_tech_table[nfc_state.tech]->listener.wait_event(timeout_ms);
}

NfcError nfc_listener_tx(const uint8_t* tx_data, size_t tx_bits) {
    return nfc_tech_table[nfc_state.tech]->listener.tx(tx_data, tx_bits);
}

NfcError nfc_listener_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    return nfc_tech_table[nfc_state.tech]->listener.rx(rx_data, rx_data_size, rx_bits);
}

NfcError nfc_listener_sleep(void) {
    return nfc_tech_table[nfc_state.tech]->listener.sleep();
}

NfcError nfc_listener_idle(void) {
    return nfc_tech_table[nfc_state.tech]->listener.idle();
}

NfcError nfc_listener_enable_rx(void) {
    st25r3916_direct_cmd(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
    return NfcErrorNone;
}

NfcError nfc_trx_reset(void) {
    st25r3916_direct_cmd(ST25R3916_CMD_STOP);
    return NfcErrorNone;
}
