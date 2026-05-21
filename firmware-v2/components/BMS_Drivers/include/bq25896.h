#pragma once

#include "bq25896_reg.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device ID
#define BQ25896_PN_ID           0x03

// VREG (Charge Voltage Estimate) Calculation
#define BQ25896_VREG_BASE_MV    3840
#define BQ25896_VREG_STEP_MV    16
#define BQ25896_VREG_MAX_MV     4208    // Safety clamp: max safe voltage for standard Li-ion cells

// ICHG (Charge Current) Calculation
#define BQ25896_ICHG_STEP_MA    64
#define BQ25896_ICHG_MAX_MA     5056

// IINLIM (Input Current Limit) Calculation
#define BQ25896_IINLIM_BASE_MA  100
#define BQ25896_IINLIM_STEP_MA  50
#define BQ25896_IINLIM_MAX_MA   3250

// VBUS Voltage Calculation
#define BQ25896_VBUS_BASE_MV    2600
#define BQ25896_VBUS_STEP_MV    100

// VSYS Voltage Calculation
#define BQ25896_VSYS_BASE_MV    2304
#define BQ25896_VSYS_STEP_MV    20

// VBAT Voltage Calculation
#define BQ25896_VBAT_BASE_MV    2304
#define BQ25896_VBAT_STEP_MV    20

// ICHGR (Actual Charge Current) Calculation
#define BQ25896_ICHGR_STEP_MA   50

// NTC Calculation
#define BQ25896_NTC_BASE_MPCT   21000
#define BQ25896_NTC_STEP_MPCT   465

// OTG Boost
#define BQ25896_BOOSTV_5V       0x08
#define BQ25896_SYS_MIN_3V      0x00

// Safe input current defaults (mA)
#define BQ25896_USB_DEFAULT_IINLIM_MA   500     // USB 2.0 configured device limit

/** Initialize Driver (registers device on the I2C bus, resets, and configures) */
esp_err_t bq25896_init(i2c_master_bus_handle_t bus_handle);

/** Set boost lim*/
esp_err_t bq25896_set_boost_lim(BoostLim boost_lim);

/** Send device into shipping mode */
esp_err_t bq25896_poweroff(void);

/** Get charging status */
ChrgStat bq25896_get_charge_status(void);

/** Is currently charging */
bool bq25896_is_charging(void);

/** Is charging completed while connected to charger */
bool bq25896_is_charging_done(void);

/** Enable charging */
esp_err_t bq25896_enable_charging(void);

/** Disable charging */
esp_err_t bq25896_disable_charging(void);

/** Enable otg */
esp_err_t bq25896_enable_otg(void);

/** Disable otg */
esp_err_t bq25896_disable_otg(void);

/** Is otg enabled */
bool bq25896_is_otg_enabled(void);

/** Get VREG (charging limit) voltage in mV */
uint16_t bq25896_get_vreg_voltage(void);

/** Set VREG (charging limit) voltage in mV
 * Valid range: 3840mV - 4208mV, in steps of 16mV
 */
esp_err_t bq25896_set_vreg_voltage(uint16_t vreg_voltage);

/** Check OTG BOOST Fault status */
bool bq25896_check_otg_fault(void);

/** Get VBUS Voltage in mV */
uint16_t bq25896_get_vbus_voltage(void);

/** Get VSYS Voltage in mV */
uint16_t bq25896_get_vsys_voltage(void);

/** Get VBAT Voltage in mV */
uint16_t bq25896_get_vbat_voltage(void);

/** Get VBAT current in mA */
uint16_t bq25896_get_vbat_current(void);

/** Get NTC voltage in mpct of REGN */
uint32_t bq25896_get_ntc_mpct(void);

/** Set Charge Current Limit in mA (ICHG) 
 * Range: 0-2048mA (steps of 64mA) or up to 5A depending on sensing
 * BQ25896: Range 0-5056mA, step 64mA.
 */
esp_err_t bq25896_set_charge_current(uint16_t current_limit_ma);

/** Set Input Current Limit in mA (IINLIM)
 * Range: 100-3250mA
 */
esp_err_t bq25896_set_input_current_limit(uint16_t current_limit_ma);

/** Get current Input Current Limit setting in mA (IINLIM) */
uint16_t bq25896_get_input_current_limit(void);

/** Check Device ID. Returns ESP_OK if valid BQ25896 */
esp_err_t bq25896_check_id(void);

/** Pet Watchdog */
esp_err_t bq25896_watchdog_reset(void);

/** Read complete charger status register (REG0B) */
void bq25896_read_status(REG0B *out);

/** Read complete fault register (REG0C). Reading clears latched fault bits. */
void bq25896_read_faults(REG0C *out);

#ifdef __cplusplus
}
#endif