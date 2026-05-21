#include "max17260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MAX17260"

static float g_rsense_ohms = MAX17260_DEFAULT_RSENSE;
static i2c_master_dev_handle_t s_max17260_dev = NULL;

static esp_err_t max17260_read_reg(uint8_t reg, uint16_t *val) {
    uint8_t data[2];
    esp_err_t ret = i2c_master_transmit_receive(s_max17260_dev, &reg, 1, data, 2, 100);
    if (ret == ESP_OK) {
        *val = (data[1] << 8) | data[0]; // Little Endian
    }
    return ret;
}

static esp_err_t max17260_write_reg(uint8_t reg, uint16_t val) {
    uint8_t data[3] = { reg, val & 0xFF, (val >> 8) & 0xFF };
    return i2c_master_transmit(s_max17260_dev, data, 3, 100);
}

esp_err_t max17260_check_communication(void) {
    uint16_t status;
    return max17260_read_reg(REG_STATUS, &status);
}

esp_err_t max17260_init(i2c_master_bus_handle_t bus_handle, const MAX17260Config* config) {
    // Register device on the I2C bus (idempotent — skips if already added)
    if (s_max17260_dev == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = MAX17260_ADDRESS,
            .scl_speed_hz    = BSP_I2C_FREQ_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_max17260_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add MAX17260 to I2C bus: %d", err);
            return err;
        }
    }

    if (config->rsense > 0.0f) {
        g_rsense_ohms = config->rsense;
    }

    uint16_t status = 0;
    esp_err_t ret = max17260_read_reg(REG_STATUS, &status);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STATUS register");
        return ret;
    }

    // Check for Power-On Reset (POR)
    if(!(status & STATUS_POR)) {
        ESP_LOGI(TAG, "Gauge already initialized, skipping config");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "POR detected, initializing gauge...");

    // 1. Wait until FSTAT.DNR is 0 (Device Not Ready)
    uint16_t fstat = 0;
    int retries = MAX17260_POR_CHECK_RETRIES;
    do {
        max17260_read_reg(REG_FSTAT, &fstat);
        vTaskDelay(pdMS_TO_TICKS(MAX17260_POR_CHECK_DELAY_MS));
        retries--;
    } while ((fstat & 0x0001) && retries > 0);

    if (retries == 0) {
        ESP_LOGE(TAG, "Gauge DNR timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Store HIBCFG
    uint16_t hibcfg_old = 0;
    max17260_read_reg(REG_HIB_CFG, &hibcfg_old);

    // Exit Hibernate Mode (per Maxim AN6595 EZ Config procedure)
    max17260_write_reg(REG_CMD, MAX17260_CMD_SOFT_WAKEUP);   // Step 1: Soft-Wakeup command
    max17260_write_reg(REG_HIB_CFG, MAX17260_HIBERNATE_DISABLE); // Step 2: Disable Hibernate
    max17260_write_reg(REG_CMD, MAX17260_CMD_CLEAR);         // Step 3: Clear command

    // Configure temperature source: use internal die temperature.
    // The battery NTC thermistor is connected to the BQ25896 charger (TS pin),
    // NOT to the MAX17260 (THM pin). Disable the thermistor bias circuit so the
    // gauge uses its internal die temp sensor instead of reading a floating/fixed THM pin.
    uint16_t config_val = 0;
    max17260_read_reg(REG_CONFIG, &config_val);
    config_val &= ~CONFIG_ETHRM;   // Disable external thermistor
    config_val &= ~CONFIG_FTHRM;   // Don't force thermistor bias
    max17260_write_reg(REG_CONFIG, config_val);
    
    // 2. Initialize configuration registers
    // DesignCap: 0.5mAh LSB per 10mOhm
    // Formula: Capacity / (0.005 / Rsense)
    uint16_t design_cap_reg = (uint16_t)(config->design_cap / (MAX17260_LSB_CAPACITY_UVH * 1.0e-3f / config->rsense)); 
    max17260_write_reg(REG_DESIGN_CAP, design_cap_reg);
    max17260_write_reg(REG_FULL_CAP_REP, design_cap_reg); // Set FullCapRep to DesignCap initially

    // IChgTerm: Set exact termination current
    // LSB is 1.5625 uV / Rsense -> ~156uA for 10mOhm. 
    // Manual says LSB 1/6400 * I_range? Better use formula: mA / (1.5625 / Rsense / 1000)
    // Simplified: mA * Rsense * 640
    uint16_t ichg_term_reg = (uint16_t)(config->i_chg_term * config->rsense * MAX17260_SCALING_ICHG_TERM);
    max17260_write_reg(REG_ICHG_TERM, ichg_term_reg);


    // VEmpty: Default to 3.3V/3.88V (0xA561) if not specified
    max17260_write_reg(REG_V_EMPTY, config->v_empty ? config->v_empty : MAX17260_LSB_V_EMPTY_DEFAULT);

    // 3. Model Refresh — preserve auto-detected CSEL (high-side/low-side sensing
    //    is determined by the IC at boot from pin wiring; do not override).
    //    VChg=0 (charge voltage ≤ 4.25V), ModelID=0 (LiCoO2/LiPo)
    uint16_t model_cfg = 0;
    max17260_read_reg(REG_MODEL_CFG, &model_cfg);
    model_cfg |= MODELCFG_REFRESH;
    max17260_write_reg(REG_MODEL_CFG, model_cfg);

    // Wait for refresh to complete
    retries = MAX17260_POR_CHECK_RETRIES;
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        max17260_read_reg(REG_MODEL_CFG, &model_cfg);
        retries--;
    } while ((model_cfg & MODELCFG_REFRESH) && retries > 0);

    // Restore HibCfg
    max17260_write_reg(REG_HIB_CFG, hibcfg_old);

    // 4. Clear POR bit
    max17260_read_reg(REG_STATUS, &status);
    max17260_write_reg(REG_STATUS, status & ~STATUS_POR);

    ESP_LOGI(TAG, "Gauge Initialized");
    return ESP_OK;
}

uint16_t max17260_get_voltage(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_VCELL, &val);
    // LSB is 78.125 uV / 16 bit
    // Actually register is 1.25mV / 16? No, LSB is 78.125uV
    return (uint16_t)(val * MAX17260_LSB_VOLTAGE_MV);
}

int32_t max17260_get_current(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_CURRENT, &val);
    int16_t sval = (int16_t)val;
    // LSB is 1.5625 uV / Rsense (uA)
    // Formula: sval * (1.5625 / Rsense)
    return (int32_t)(sval * (MAX17260_LSB_CURRENT_UV / g_rsense_ohms)); 
}

uint16_t max17260_get_soc(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_REP_SOC, &val);
    return val >> 8; // Upper byte is %
}

uint16_t max17260_get_capacity(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_REP_CAP, &val);
    // LSB is 5.0 uVh / Rsense (mAh)
    // Factor = 0.005 / Rsense
    return (uint16_t)(val * (MAX17260_LSB_CAPACITY_UVH * 1.0e-3f / g_rsense_ohms)); 
}

uint16_t max17260_get_tte(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_TTE, &val);
    // LSB is 5.625 seconds
    if (val == 0xFFFF) return 0xFFFF; // Unknown
    return (uint16_t)((val * MAX17260_LSB_TIMER_SEC) / 60.0);
}

float max17260_get_temperature(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_TEMP, &val);
    int16_t sval = (int16_t)val;
    // LSB is 1/256 C
    return sval * MAX17260_LSB_TEMP_C;
}

uint16_t max17260_get_full_capacity(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_FULL_CAP_REP, &val);
    return (uint16_t)(val * (MAX17260_LSB_CAPACITY_UVH * 1.0e-3f / g_rsense_ohms)); 
}


uint8_t max17260_get_age(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_AGE, &val);
    return (uint8_t)(val >> 8);
}

int32_t max17260_get_avg_current(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_AVG_CURRENT, &val);
    int16_t sval = (int16_t)val;
    // LSB is 1.5625 uV / Rsense (uA)
    return (int32_t)(sval * (MAX17260_LSB_CURRENT_UV / g_rsense_ohms));
}

uint16_t max17260_get_cycles(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_CYCLES, &val);
    // LSB is 1% (0x0064 = 1.00 cycle)
    return val / 100;
}

uint16_t max17260_get_ttf(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_TTF, &val);
    // LSB is 5.625 seconds
    if (val == 0xFFFF) return 0xFFFF; // Unknown / not charging
    return (uint16_t)((val * MAX17260_LSB_TIMER_SEC) / 60.0);
}

uint16_t max17260_get_design_capacity(void) {
    uint16_t val = 0;
    max17260_read_reg(REG_DESIGN_CAP, &val);
    return (uint16_t)(val * (MAX17260_LSB_CAPACITY_UVH * 1.0e-3f / g_rsense_ohms));
}
