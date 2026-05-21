#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// BQ25896 Default I2C Address
#define BQ25896_ADDRESS     0x6B

// Helper for bitfield packing
#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

// --- Register Definitions ---

typedef struct {
    uint8_t IINLIM : 6; // Input Current Limit, mA, offset: +100mA
    bool EN_ILIM   : 1; // Enable ILIM Pin
    bool EN_HIZ    : 1; // Enable HIZ Mode
} PACKED REG00;

typedef enum {
    Bhot34 = 0b00, 
    Bhot37 = 0b01, 
    Bhot31 = 0b10, 
    BhotDisable = 0b11, 
} Bhot;

typedef struct {
    uint8_t VINDPM_OS : 5; 
    bool BCOLD        : 1; 
    Bhot BHOT         : 2; 
} PACKED REG01;

typedef struct {
    bool AUTO_DPDM_EN : 1; 
    bool FORCE_DPDM   : 1; 
    uint8_t RES       : 2; 
    bool ICO_EN       : 1; 
    bool BOOST_FREQ   : 1; 
    bool CONV_RATE    : 1; 
    bool CONV_START   : 1; 
} PACKED REG02;

typedef struct {
    bool MIN_VBAT_SEL : 1; 
    uint8_t SYS_MIN   : 3; // Offset +3.0V
    bool CHG_CONFIG   : 1; 
    bool OTG_CONFIG   : 1; 
    bool WD_RST       : 1; 
    bool BAT_LOADEN   : 1; 
} PACKED REG03;

typedef struct {
    uint8_t ICHG  : 7; // Fast Charge Current
    bool EN_PUMPX : 1; 
} PACKED REG04;

typedef struct {
    uint8_t ITERM   : 4; // Termination Current
    uint8_t IPRECHG : 4; // Precharge Current
} PACKED REG05;

typedef struct {
    bool VRECHG  : 1; 
    bool BATLOWV : 1; 
    uint8_t VREG : 6; // Charge Voltage Limit
} PACKED REG06;

typedef enum {
    WatchdogDisable = 0b00,
    Watchdog40 = 0b01,
    Watchdog80 = 0b10,
    Watchdog160 = 0b11,
} Watchdog;

typedef struct {
    bool JEITA_ISET    : 1; 
    uint8_t CHG_TIMER  : 2; 
    bool EN_TIMER      : 1; 
    Watchdog WATCHDOG  : 2; 
    bool STAT_DIS      : 1; 
    bool EN_TERM       : 1; 
} PACKED REG07;

typedef struct {
    uint8_t TREG     : 2; 
    uint8_t VCLAMP   : 3; 
    uint8_t BAT_COMP : 3; 
} PACKED REG08;

typedef struct {
    bool PUMPX_DN      : 1; 
    bool PUMPX_UP      : 1; 
    bool BATFET_RST_EN : 1; 
    bool BATFET_DLY    : 1; 
    bool JEITA_VSET    : 1; 
    bool BATFET_DIS    : 1; 
    bool TMR2X_EN      : 1; 
    bool FORCE_ICO     : 1; 
} PACKED REG09;

typedef enum {
    BoostLim_500 = 0b000,
    BoostLim_750 = 0b001,
    BoostLim_1200 = 0b010,
    BoostLim_1400 = 0b011,
    BoostLim_1650 = 0b100,
    BoostLim_1875 = 0b101,
    BoostLim_2150 = 0b110,
    BoostLim_Rsvd = 0b111,
} BoostLim;

typedef struct {
    uint8_t BOOST_LIM : 3; 
    bool PFM_OTG_DIS  : 1; 
    uint8_t BOOSTV    : 4; 
} PACKED REG0A;

typedef enum {
    VBusStatNo = 0b000,
    VBusStatUSB = 0b001,
    VBusStatExternal = 0b010,
    VBusStatOTG = 0b111,
} VBusStat;

typedef enum {
    ChrgStatNo = 0b00,
    ChrgStatPre = 0b01,
    ChrgStatFast = 0b10,
    ChrgStatDone = 0b11,
} ChrgStat;

typedef struct {
    bool VSYS_STAT     : 1; 
    bool RES           : 1; 
    bool PG_STAT       : 1; 
    ChrgStat CHRG_STAT : 2; 
    VBusStat VBUS_STAT : 3; 
} PACKED REG0B;

typedef enum {
    ChrgFaultNO = 0b00,
    ChrgFaultIN = 0b01,
    ChrgFaultTH = 0b10,
    ChrgFaultTIM = 0b11,
} ChrgFault;

typedef enum {
    NtcFaultNo = 0b000,
    NtcFaultWarm = 0b010,
    NtcFaultCool = 0b011,
    NtcFaultCold = 0b101,
    NtcFaultHot = 0b110,
} NtcFault;

typedef struct {
    NtcFault NTC_FAULT   : 3; 
    bool BAT_FAULT       : 1; 
    ChrgFault CHRG_FAULT : 2; 
    bool BOOST_FAULT     : 1; 
    bool WATCHDOG_FAULT  : 1; 
} PACKED REG0C;

typedef struct {
    uint8_t VINDPM    : 7; 
    bool FORCE_VINDPM : 1; 
} PACKED REG0D;

typedef struct {
    uint8_t BATV    : 7; 
    bool THERM_STAT : 1; 
} PACKED REG0E;

typedef struct {
    uint8_t SYSV : 7; 
    uint8_t RES  : 1; 
} PACKED REG0F;

typedef struct {
    uint8_t TSPCT : 7; 
    uint8_t RES   : 1; 
} PACKED REG10;

typedef struct {
    uint8_t VBUSV : 7; 
    bool VBUS_GD  : 1; 
} PACKED REG11;

typedef struct {
    uint8_t ICHGR : 7; 
    uint8_t RES   : 1; 
} PACKED REG12;

typedef struct {
    uint8_t IDPM_LIM : 6; 
    bool IDPM_STAT   : 1; 
    bool VDPM_STAT   : 1; 
} PACKED REG13;

typedef struct {
    uint8_t DEV_REV    : 2; 
    bool TS_PROFILE    : 1; 
    uint8_t PN         : 3; 
    bool ICO_OPTIMIZED : 1; 
    bool REG_RST       : 1; 
} PACKED REG14;

#ifdef __cplusplus
}
#endif