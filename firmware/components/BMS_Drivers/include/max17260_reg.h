#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MAX17260_ADDRESS     0x36 // 7-bit address (0x6C shifted right)

// Register Addresses
#define REG_STATUS          0x00
#define REG_VALRT_TH        0x01
#define REG_TALRT_TH        0x02
#define REG_SALRT_TH        0x03
#define REG_AT_RATE         0x04
#define REG_REP_CAP         0x05
#define REG_REP_SOC         0x06
#define REG_AGE             0x07
#define REG_TEMP            0x08
#define REG_VCELL           0x09
#define REG_CURRENT         0x0A
#define REG_AVG_CURRENT     0x0B
#define REG_FULL_CAP_REP    0x10
#define REG_TTE             0x11
#define REG_CYCLES          0x17
#define REG_DESIGN_CAP      0x18
#define REG_AVG_VCELL       0x19
#define REG_MAX_MIN_TEMP    0x1A
#define REG_MAX_MIN_VOLT    0x1B
#define REG_MAX_MIN_CURR    0x1C
#define REG_CONFIG          0x1D
#define REG_ICHG_TERM       0x1E
#define REG_TTF             0x20
#define REG_V_EMPTY         0x3A
#define REG_FSTAT           0x3D
#define REG_CMD             0x60  // Command register (soft-wakeup / hibernate exit)
#define REG_HIB_CFG         0xBA
#define REG_CONFIG2         0xBB
#define REG_MODEL_CFG       0xDB

// Status Bit Definitions
#define STATUS_POR          (1u << 1)
#define STATUS_IMIN         (1u << 2)
#define STATUS_BST          (1u << 3)
#define STATUS_IMAX         (1u << 6)

// Config Register (0x1D) Bit Definitions — default POR value is 0x2210
#define CONFIG_BER          (1u << 0)   // Battery removal detection enable
#define CONFIG_BEI          (1u << 1)   // Battery insertion detection enable
#define CONFIG_AEN          (1u << 2)   // Alert enable on ALRT pin
#define CONFIG_FTHRM        (1u << 3)   // Force thermistor bias switch always on
#define CONFIG_ETHRM        (1u << 4)   // Enable external thermistor (THM pin)
#define CONFIG_AINSH        (1u << 7)   // AIN input select
#define CONFIG_TEN          (1u << 8)   // Temperature measurement enable
#define CONFIG_TALRT_EN     (1u << 9)   // Temperature alert enable
#define CONFIG_DSOCEN       (1u << 10)  // 1% SOC change alert enable
#define CONFIG_PBEN         (1u << 14)  // Push-button enable

// ModelCfg Bit Definitions (Register 0xDB)
#define MODELCFG_REFRESH    (1u << 15)  // Trigger model refresh
#define MODELCFG_R100       (1u << 13)  // 1 = 100kΩ NTC, 0 = 10kΩ NTC
#define MODELCFG_VCHG       (1u << 10)  // 1 = charge voltage > 4.25V
#define MODELCFG_CSEL       (1u << 2)   // 1 = high-side sensing (CSN-CSPH), 0 = low-side (CSN-CSPL)

#ifdef __cplusplus
}
#endif