#pragma once

#include "max17260_reg.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration
#define MAX17260_I2C_PORT BSP_I2C_PORT

// Constants
#define MAX17260_DEFAULT_RSENSE       0.010f

#define MAX17260_POR_CHECK_RETRIES    50
#define MAX17260_POR_CHECK_DELAY_MS   10

#define MAX17260_CMD_SOFT_WAKEUP        0x0090  // Write to REG_CMD to exit hibernate
#define MAX17260_CMD_CLEAR              0x0000  // Write to REG_CMD after soft-wakeup
#define MAX17260_HIBERNATE_DISABLE      0x0000  // Write to REG_HIB_CFG to disable hibernate

#define MAX17260_LSB_VOLTAGE_MV       0.078125f // 78.125uV, returns in mV
#define MAX17260_LSB_CURRENT_UV       1.5625f   // 1.5625uV / Rsense
#define MAX17260_LSB_CAPACITY_UVH     5.0f      // 5.0uVh / Rsense
#define MAX17260_LSB_TIMER_SEC        5.625f
#define MAX17260_LSB_TEMP_C           (1.0f/256.0f)
#define MAX17260_SCALING_ICHG_TERM    640.0f    // Derived scaling factor

// Default register values
#define MAX17260_LSB_V_EMPTY_DEFAULT  0xA561    // ~3.3V / 3.88V default setting

typedef struct {
    uint16_t design_cap;    // mAh
    uint16_t v_empty;       // 10mV resolution (e.g. 0xA561 for 3.3V)
    uint16_t i_chg_term;    // mA
    float rsense;           // Ohms (e.g., 0.010 for 10mOhm)
} MAX17260Config;

/**
 * Initialize the MAX17260.
 * Handles POR detection and ModelGauge m5 EZ initialization.
 */
esp_err_t max17260_init(const MAX17260Config* config);

/** Get Voltage in mV */
uint16_t max17260_get_voltage(void);

/** Get Current in uA */
int32_t max17260_get_current(void);

/** Get State of Charge in % (0-100) */
uint16_t max17260_get_soc(void);

/** Get Remaining Capacity in mAh */
uint16_t max17260_get_capacity(void);

/** Get Full Reported Capacity in mAh */
uint16_t max17260_get_full_capacity(void);

/** Get Battery Age in % (100 = New) */
uint8_t max17260_get_age(void);

/** Get Time to Empty in Minutes */
uint16_t max17260_get_tte(void);

/** Get Temperature in degrees C (float) */
float max17260_get_temperature(void);

/** Get Average Current in uA (signed) */
int32_t max17260_get_avg_current(void);

/** Get Battery Cycle Count */
uint16_t max17260_get_cycles(void);

/** Get Time to Full in minutes (0xFFFF = unknown/not charging) */
uint16_t max17260_get_ttf(void);

/** Get Design Capacity in mAh (as configured) */
uint16_t max17260_get_design_capacity(void);

#ifdef __cplusplus
}
#endif