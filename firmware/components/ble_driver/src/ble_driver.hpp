#ifndef BLE_DRIVER_H
#define BLE_DRIVER_H

#include <stdio.h>
#include "NimBLEAdvertising.h"
#include "NimBLEDevice.h"
#include "NimBLEConnInfo.h"
#include "BNO08xGlobalTypes.hpp"
#include "NimBLEServer.h"

#define DEVICE_NAME "PetPulse-0001"

/* Service UUIDS */
#define VITALS_UUID "792C45E0-7B95-4A4D-8BC2-6D04809BB406"        // Vital signs service (HR,BR)
#define ACTIVITY_UUID "792C45E1-7B95-4A4D-8BC2-6D04809BB406"      // Motion/Activity Service (Accel,Gyro,Magf,StepCount,ActivityClassfier)
#define BATTERY_UUID "180F"                                       // Bluetooth assigned numbers battery service UUID = 0x180F
#define CUR_TIME_SERVICE_UUID "1805"                              // Bluetooth assigned numbers current time service UUID = 0x1805
#define ENVIRO_UUID "181A"

/* Vital Signs Service Charcteristics UUIDS*/
#define HR_UUID "792C45E2-7B95-4A4D-8BC2-6D04809BB406"            // Heart Rate
#define BR_UUID "792C45E3-7B95-4A4D-8BC2-6D04809BB406"            // Breath Rate 

/* Activity Service Charcteristics UUIDS */
#define ACCEL_UUID "792C45E4-7B95-4A4D-8BC2-6D04809BB406"         // Accelorometer
#define GYRO_UUID "792C45E5-7B95-4A4D-8BC2-6D04809BB406"          // Gyroscope
#define MAGF_UUID "792C45E6-7B95-4A4D-8BC2-6D04809BB406"          // Magnetometer
#define STEP_COUNT_UUID "792C45E7-7B95-4A4D-8BC2-6D04809BB406"     // Step Counter
#define ACTIVITY_CLASS_UUID "792C45E8-7B95-4A4D-8BC2-6D04809BB406" // Activity Classifier

/* Environmental Sensor Service Charcteristics */
#define TEMP_UUID "2A6E"                                           // Bluetooth assigned numbers temperature charcteristic UUID = 0x2A6E
#define HUMIDITY_UUID "2A6F"                                       // Bluetooth assigned numbers humidity charcteristic UUID = 0x2A6F

/* Battery Service Charcteristics UUIDS */
#define BATTERY_LEVEL_STAT_UUID "2BED"                             // Bluetooth assigned numbers battery level status (charge state bits, wired external power source fields) charcteristic UUID = 0x2BED
#define BATTERY_ENERGY_STAT_UUID "2BF0"                            // Bluetooth assigned numbers battery energy status (present terminal voltage field) charcteristic UUID = 0x2BF0
#define BATTERY_TIME_STAT_UUID "2BEE"                              // Bluetooth assigned numbers battery time status (time to recharge/discharge fields) charcteristic UUID = 0x2BEE
#define BATTERY_HEALTH_STAT_UUID "2BEA"                            // Bluetooth assigned numbers battery health status(summary and current temp fields) charcteristic UUID = 0x2BEA

/* Current Time Service Charcteristics */
#define CUR_TIME_UUID "2A2B"                                       // Bluetooth assigned numbers current time (strip to only send UTC) charcteristic UUID = 0x2A2B


bool syncSysTime(NimBLEAttValue& value);

class BleServer  {
    private: 

        /* BLE Server Pointer */
        NimBLEServer *pServer = nullptr;
       /* Vital Signs Service Pointer and Characteristics */
        NimBLEService *pVitalsService = nullptr;
            NimBLECharacteristic *pHeartRate = nullptr;
            NimBLECharacteristic *pBreathRate = nullptr;

        /* IMUService Pointer and Characteristics */
        NimBLEService *pActivityService = nullptr;
            NimBLECharacteristic *pAccel = nullptr;
            NimBLECharacteristic *pGyro= nullptr;
            NimBLECharacteristic *pMagf = nullptr;
            NimBLECharacteristic *pStepCount = nullptr;
            NimBLECharacteristic *pActivityClass = nullptr;

        /* Environmental Sensors Service */
        NimBLEService *pEnviroService = nullptr;
            NimBLECharacteristic *pTemp = nullptr;
            NimBLECharacteristic *pHumidity = nullptr;


        /* Battery Service Pointer and Characteristics */
        NimBLEService *pBatteryService = nullptr;
            NimBLECharacteristic *pBatteryLevel = nullptr;
            NimBLECharacteristic *pBatteryEnergy = nullptr;
            NimBLECharacteristic *pBatteryTime = nullptr;
            NimBLECharacteristic *pBatteryHealth = nullptr;

        /* Current Time Service Pointer and Characteristics */
        NimBLEService *pCurTimeService = nullptr;
            NimBLECharacteristic *pCurTime = nullptr;

        /* Accel Struct */ 
        struct BleAccel_t {
            float x;
            float y;
            float z;
            uint8_t accuracy;
        }__attribute__((packed));

        BleAccel_t _accel;

        /* Gyro Struct */ 
        struct BleGyro_t {
            float x;
            float y;
            float z;
            uint8_t accuracy;
        }__attribute__((packed));

        BleGyro_t _gyro;

        /* Magf Struct */ 
        struct BleMagf_t {
            float x;
            float y;
            float z;
            uint8_t accuracy;
        }__attribute__((packed));

        BleMagf_t _magf;

        /* Step Count Struct */ 
        struct BleStepCount_t {
            uint32_t latency;
            uint16_t steps;
        }__attribute__((packed));

        BleStepCount_t _stepCount;

        /* Activity Classifier Struct */ 
        struct BleActivityClass_t {
            uint8_t confidence[10];
            uint8_t mostLikelyState;
            uint8_t accuracy;
        }__attribute__((packed));

        BleActivityClass_t _activityClass;

        /* Battery Level Status Struct */
        struct BatteryLevel_t {
            uint8_t flags = 0x06; //Battery Level and Additional Status bits set
            uint16_t power_state = 0x0001; //Default 1: Battery Present
            uint8_t battery_level = 0x00;
            uint8_t additional_status = 0x00;
        }__attribute__((packed));

        BatteryLevel_t _batteryLevel;

        /* Battery Level Status Struct */
        struct BatteryEnergy_t {
            uint8_t flags = 0x01; //Terminal voltage bit set
            uint16_t curr_voltage = 0x0000; //medfloat16 let client interpret
        }__attribute__((packed));

        BatteryEnergy_t _batteryEnergy;

        /* Battery Level Status Struct */
        struct BatteryTime_t {
            uint8_t flags = 0x03; //Time until recharged bit set
            uint8_t time_to_discharge[3] = {0x00, 0x00, 0x00}; // uint8_t[3] to represent 24 bits
            uint8_t time_to_recharge[3] = {0x00, 0x00, 0x00}; // uint8_t[3] to represent 24 bits
        }__attribute__((packed));

        BatteryTime_t _batteryTime;

        /* Battery Level Status Struct */
        struct BatteryHealth_t {
            uint8_t flags = 0x03; //Summary and current temp bits set
            uint8_t health_summary = 0x00;
            int8_t current_temp = 0x00;
        }__attribute__((packed));

        BatteryHealth_t _batteryHealth;

        struct CurrentTime_t {
            uint16_t year = 0x0000;
            uint8_t month = 0x00;
            uint8_t day = 0x00;
            uint8_t hours = 0x00;
            uint8_t minutes = 0x00;
            uint8_t seconds = 0x00;
            uint8_t weekday = 0x00;
            uint8_t fraction_ms = 0x00;  // 1/256th of a second ~ 3.906ms precision
            uint8_t adjust_reason = 0x00;
        }__attribute__((packed));

        CurrentTime_t _currentTime;

        public: 
            bool init(int8_t tx_power);
            bool deinit();
            bool restart();
            bool startAdvertising();
            bool isAdvertising();
            void setTXPower(int8_t tx_power);

            /* Vitals Charcteristic Setters */
            void setHR(bool notify = true);
            void setBR(bool notify = true);

            /* Acitivty Charcteristic Setters */
            void setAccel(bool notify = true);
            void setGyro(bool notify = true);
            void setMagf(bool notify = true);
            void setStepCount(bool notify = true) ;
            void setActivityClass(bool notify = true);

            /* Environmental Sensor Charcteristic Setters */
            void setTemp(bool notify = true); //sint16_t temperature resolution: 0.1°C
            void setHumidity(bool notify = true); //uint16_t humidity resolution: 0.01%

            // TO-DO: Add more functions 

            /* Battery Level Status Functions */
            void setPowerState(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault);

            /* Battery Level Setter */
            void setBatteryLevel(uint8_t battery_level);

            /* Additional Status Field Bits Setter*/
            void setAdditionalStatus(uint8_t service_req, uint8_t batt_fault);

            /* Present Terminal Voltage Stter*/
            void setCurrVoltage(uint16_t curr_voltage);

            /*Battery Time Status Setters*/
            void setTimeDischarge(uint8_t time_to_discharge[3]);
            void setTimeRecharge(uint8_t time_to_recharge[3]);

            /* Battery Health Status Setter */
            void setHealthSummary(uint8_t health_summary);
            void setCurrentTemp(int8_t current_temp);
        
            /** 
            Battery Charcteristic Value Setters
            @param  notify - Set NimBLE to notify on this charcteristic
            @param indicate - Set NimBLE to indicate on this charcteristic
            */
            void setBattLevelValue(bool notify = false, bool indicate = true);
            void setBattEnergyValue(bool notify = false, bool indicate = true);
            void setBattTimeValue(bool notify = false, bool indicate = true);
            void setBattHealthValue(bool notify = false, bool indicate = true);

};

extern BleServer bleServer;

class ServerCallbacks : public NimBLEServerCallbacks {
   void onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo) override;
   void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
   void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
};

extern ServerCallbacks serverCallbacks;

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
};

extern CharacteristicCallbacks chrCallbacks;

#endif // BLE_DRIVER_H