#include "bq25896.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "BQ25896"

typedef struct {
    REG00 r00;
    REG01 r01;
    REG02 r02;
    REG03 r03;
    REG04 r04;
    REG05 r05;
    REG06 r06;
    REG07 r07;
    REG08 r08;
    REG09 r09;
    REG0A r0A;
    REG0B r0B;
    REG0C r0C;
    REG0D r0D;
    REG0E r0E;
    REG0F r0F;
    REG10 r10;
    REG11 r11;
    REG12 r12;
    REG13 r13;
    REG14 r14;
} PACKED bq25896_regs_t;

static bq25896_regs_t bq25896_regs;

// Helper function to read a register
static esp_err_t bq25896_read_reg(uint8_t reg_addr, uint8_t *data) {
    return i2c_master_write_read_device(BQ25896_I2C_PORT, BQ25896_ADDRESS, &reg_addr, 1, data, 1, pdMS_TO_TICKS(100));
}

// Helper function to write a register
static esp_err_t bq25896_write_reg(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(BQ25896_I2C_PORT, BQ25896_ADDRESS, write_buf, 2, pdMS_TO_TICKS(100));
}

// Helper to read all registers into shadow struct
static esp_err_t bq25896_read_all_regs(void) {
    uint8_t reg_addr = 0x00;
    return i2c_master_write_read_device(BQ25896_I2C_PORT, BQ25896_ADDRESS, &reg_addr, 1, (uint8_t*)&bq25896_regs, sizeof(bq25896_regs), pdMS_TO_TICKS(100));
}

esp_err_t bq25896_init(void) {
    esp_err_t ret;

    // Reset device
    // Note: r14 starts zeroed, so we only set the RST bit. Writable bits are only RST.
    bq25896_regs.r14.REG_RST = 1;
    ret = bq25896_write_reg(0x14, *(uint8_t*)&bq25896_regs.r14);
    if (ret != ESP_OK) return ret;

    // REG_RST is self-clearing; allow chip time to complete internal reset
    vTaskDelay(pdMS_TO_TICKS(10));

    // Readout all registers
    ret = bq25896_read_all_regs();
    if (ret != ESP_OK) return ret;

    // Verify Device ID
    if (bq25896_check_id() != ESP_OK) {
        ESP_LOGE(TAG, "BQ25896 ID Check Failed");
        return ESP_FAIL;
    }

    // Configure ADC: Poll ADC forever
    bq25896_regs.r02.CONV_START = 1;
    bq25896_regs.r02.CONV_RATE = 1;
    ret = bq25896_write_reg(0x02, *(uint8_t*)&bq25896_regs.r02);
    if (ret != ESP_OK) return ret;

    // Disable Watchdog for easier debugging, or set to appropriate timeout
    bq25896_regs.r07.WATCHDOG = WatchdogDisable;
    ret = bq25896_write_reg(0x07, *(uint8_t*)&bq25896_regs.r07);
    if (ret != ESP_OK) return ret;

    // OTG power configuration (Boost mode: 5.0V, 1.4A)
    bq25896_regs.r0A.BOOSTV = BQ25896_BOOSTV_5V; 
    bq25896_regs.r0A.BOOST_LIM = BoostLim_1400; 
    ret = bq25896_write_reg(0x0A, *(uint8_t*)&bq25896_regs.r0A);
    if (ret != ESP_OK) return ret;

    // Disable minimum system voltage limit (set to lowest) to avoid power drain when battery is low
    bq25896_regs.r03.SYS_MIN = BQ25896_SYS_MIN_3V; 
    ret = bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
    if (ret != ESP_OK) return ret;


    // Default Charge Config (2A Fast Charge, 500mA Input Limit)
    // bq25896_set_charge_current(2000); 
    // bq25896_set_input_current_limit(500);

    // Update shadow regs
    bq25896_read_all_regs();

    ESP_LOGI(TAG, "Initialized BQ25896. CHG Status: %d, VBUS Status: %d", 
             bq25896_regs.r0B.CHRG_STAT, bq25896_regs.r0B.VBUS_STAT);

    return ret;
}

esp_err_t bq25896_set_boost_lim(BoostLim boost_lim) {
    bq25896_regs.r0A.BOOST_LIM = boost_lim;
    return bq25896_write_reg(0x0A, *(uint8_t*)&bq25896_regs.r0A);
}

esp_err_t bq25896_poweroff(void) {
    bq25896_read_reg(0x09, (uint8_t*)&bq25896_regs.r09);
    bq25896_regs.r09.BATFET_DIS = 1;
    return bq25896_write_reg(0x09, *(uint8_t*)&bq25896_regs.r09);
}

ChrgStat bq25896_get_charge_status(void) {
    bq25896_read_reg(0x0B, (uint8_t*)&bq25896_regs.r0B);
    return bq25896_regs.r0B.CHRG_STAT;
}

bool bq25896_is_charging(void) {
    return bq25896_get_charge_status() != ChrgStatNo;
}

bool bq25896_is_charging_done(void) {
    return bq25896_get_charge_status() == ChrgStatDone;
}

esp_err_t bq25896_enable_charging(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    bq25896_regs.r03.CHG_CONFIG = 1;
    return bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
}

esp_err_t bq25896_disable_charging(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    bq25896_regs.r03.CHG_CONFIG = 0;
    return bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
}

esp_err_t bq25896_enable_otg(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    bq25896_regs.r03.OTG_CONFIG = 1;
    return bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
}

esp_err_t bq25896_disable_otg(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    bq25896_regs.r03.OTG_CONFIG = 0;
    return bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
}

bool bq25896_is_otg_enabled(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    return bq25896_regs.r03.OTG_CONFIG;
}

uint16_t bq25896_get_vreg_voltage(void) {
    bq25896_read_reg(0x06, (uint8_t*)&bq25896_regs.r06);
    return (uint16_t)bq25896_regs.r06.VREG * BQ25896_VREG_STEP_MV + BQ25896_VREG_BASE_MV;
}

esp_err_t bq25896_set_vreg_voltage(uint16_t vreg_voltage) {
    if(vreg_voltage < BQ25896_VREG_BASE_MV) vreg_voltage = BQ25896_VREG_BASE_MV;
    if(vreg_voltage > BQ25896_VREG_MAX_MV) vreg_voltage = BQ25896_VREG_MAX_MV;
    
    bq25896_read_reg(0x06, (uint8_t*)&bq25896_regs.r06);
    bq25896_regs.r06.VREG = (uint8_t)((vreg_voltage - BQ25896_VREG_BASE_MV) / BQ25896_VREG_STEP_MV);
    return bq25896_write_reg(0x06, *(uint8_t*)&bq25896_regs.r06);
}

esp_err_t bq25896_set_charge_current(uint16_t current_limit_ma) {
    // Range 0 - 5056mA, Step 64mA
    if(current_limit_ma > BQ25896_ICHG_MAX_MA) current_limit_ma = BQ25896_ICHG_MAX_MA;
    
    bq25896_read_reg(0x04, (uint8_t*)&bq25896_regs.r04);
    bq25896_regs.r04.ICHG = current_limit_ma / BQ25896_ICHG_STEP_MA;
    return bq25896_write_reg(0x04, *(uint8_t*)&bq25896_regs.r04);
}

esp_err_t bq25896_set_input_current_limit(uint16_t current_limit_ma) {
    // Range 100 - 3250mA, Step 50mA
    if(current_limit_ma < BQ25896_IINLIM_BASE_MA) current_limit_ma = BQ25896_IINLIM_BASE_MA;
    if(current_limit_ma > BQ25896_IINLIM_MAX_MA) current_limit_ma = BQ25896_IINLIM_MAX_MA;
    
    bq25896_read_reg(0x00, (uint8_t*)&bq25896_regs.r00);
    // Value = (mA - 100) / 50
    bq25896_regs.r00.IINLIM = (current_limit_ma - BQ25896_IINLIM_BASE_MA) / BQ25896_IINLIM_STEP_MA;
    return bq25896_write_reg(0x00, *(uint8_t*)&bq25896_regs.r00);
}

esp_err_t bq25896_check_id(void) {
    bq25896_read_reg(0x14, (uint8_t*)&bq25896_regs.r14);
    // BQ25896 has PN = 011 (3)
    if (bq25896_regs.r14.PN == BQ25896_PN_ID) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "ID Mismatch: Found %d", bq25896_regs.r14.PN);
    // Return OK for now to avoid blocking unknown clones, but warn
    // return ESP_ERR_INVALID_VERSION; 
    return ESP_OK; 
}

bool bq25896_check_otg_fault(void) {
    bq25896_read_reg(0x0C, (uint8_t*)&bq25896_regs.r0C);
    return bq25896_regs.r0C.BOOST_FAULT;
}

uint16_t bq25896_get_vbus_voltage(void) {
    bq25896_read_reg(0x11, (uint8_t*)&bq25896_regs.r11);
    if(bq25896_regs.r11.VBUS_GD) {
        return (uint16_t)bq25896_regs.r11.VBUSV * BQ25896_VBUS_STEP_MV + BQ25896_VBUS_BASE_MV;
    }
    return 0;
}

uint16_t bq25896_get_vsys_voltage(void) {
    bq25896_read_reg(0x0F, (uint8_t*)&bq25896_regs.r0F);
    return (uint16_t)bq25896_regs.r0F.SYSV * BQ25896_VSYS_STEP_MV + BQ25896_VSYS_BASE_MV;
}

uint16_t bq25896_get_vbat_voltage(void) {
    bq25896_read_reg(0x0E, (uint8_t*)&bq25896_regs.r0E);
    return (uint16_t)bq25896_regs.r0E.BATV * BQ25896_VBAT_STEP_MV + BQ25896_VBAT_BASE_MV;
}

uint16_t bq25896_get_vbat_current(void) {
    bq25896_read_reg(0x12, (uint8_t*)&bq25896_regs.r12);
    return (uint16_t)bq25896_regs.r12.ICHGR * BQ25896_ICHGR_STEP_MA;
}

uint32_t bq25896_get_ntc_mpct(void) {
    bq25896_read_reg(0x10, (uint8_t*)&bq25896_regs.r10);
    return (uint32_t)bq25896_regs.r10.TSPCT * BQ25896_NTC_STEP_MPCT + BQ25896_NTC_BASE_MPCT;
}


esp_err_t bq25896_watchdog_reset(void) {
    bq25896_read_reg(0x03, (uint8_t*)&bq25896_regs.r03);
    bq25896_regs.r03.WD_RST = 1;
    return bq25896_write_reg(0x03, *(uint8_t*)&bq25896_regs.r03);
}

void bq25896_read_status(REG0B *out) {
    bq25896_read_reg(0x0B, (uint8_t*)out);
}

void bq25896_read_faults(REG0C *out) {
    bq25896_read_reg(0x0C, (uint8_t*)out);
}