#ifndef BLE_DRIVER_H
#define BLE_DRIVER_H

#include <stdio.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BLE_AUTHENTICATED_BIT  (1 << 0)
#define BLE_TIME_SYNCED_BIT    (1 << 1)

#include "NimBLEAdvertising.h"
#include "NimBLEDevice.h"
#include "NimBLEConnInfo.h"
#include "BNO08xGlobalTypes.hpp"
#include "NimBLEServer.h"
#include <sys/time.h>

#define DEVICE_NAME "PetPulse-0001"

/* Service UUIDS */
#define S_VITALS_UUID "792C45E0-7B95-4A4D-8BC2-6D04809BB406"        // Vital signs service (HR,BR)
#define S_MOTION_UUID "792C45E1-7B95-4A4D-8BC2-6D04809BB406"        // Motion/Activity Service (Accel,Gyro,Magf,StepCount,ActivityClassfier)
#define S_BATTERY_UUID "180F"                                       // Bluetooth assigned numbers battery service UUID = 0x180F
#define S_CUR_TIME_SERVICE_UUID "1805"                              // Bluetooth assigned numbers current time service UUID = 0x1805
#define S_ENVIRO_UUID "792C45E2-7B95-4A4D-8BC2-6D04809BB406" 

/* Vital Signs Service Characteristics UUIDS*/
#define C_VITALS_UUID "792C45E3-7B95-4A4D-8BC2-6D04809BB406"            // Breath Rate 

/* Activity Service Characteristics UUIDS */
#define C_RAW_UUID "792C45E4-7B95-4A4D-8BC2-6D04809BB406"            //Accel,Gyro,Magf,RV           
#define C_ACTIVITY_UUID "792C45E5-7B95-4A4D-8BC2-6D04809BB406"       // Step Counter, Activity Classifier
#define C_MODE_UUID "792C45E6-7B95-4A4D-8BC2-6D04809BB406"           // Mode

/* Environmental Sensor Service Characteristics */
#define C_TEMP_UUID "2A6E"                                           // Bluetooth assigned numbers temperature charcteristic UUID = 0x2A6E
#define C_HUMIDITY_UUID "2A6F"                                       // Bluetooth assigned numbers humidity charcteristic UUID = 0x2A6F

/* Battery Service Characteristics UUIDS */
#define BATTERY_LEVEL_STAT_UUID "2BED"                             // Bluetooth assigned numbers battery level status (charge state bits, wired external power source fields) charcteristic UUID = 0x2BED
#define BATTERY_ENERGY_STAT_UUID "2BF0"                            // Bluetooth assigned numbers battery energy status (present terminal voltage field) charcteristic UUID = 0x2BF0
#define BATTERY_TIME_STAT_UUID "2BEE"                              // Bluetooth assigned numbers battery time status (time to recharge/discharge fields) charcteristic UUID = 0x2BEE
#define BATTERY_HEALTH_STAT_UUID "2BEA"                            // Bluetooth assigned numbers battery health status(summary and current temp fields) charcteristic UUID = 0x2BEA

/* Current Time Service Characteristics */
#define CUR_TIME_UUID "2A2B"                                       // Bluetooth assigned numbers current time (strip to only send UTC) charcteristic UUID = 0x2A2B


struct Timestamp_t {
    uint16_t year = 0x0000;
    uint8_t month = 0x00;
    uint8_t day = 0x00;
    uint8_t hours = 0x00;
    uint8_t minutes = 0x00;
    uint8_t seconds = 0x00;
    uint16_t m_seconds = 0x00;
}__attribute__((packed));

enum Mode {
    Background = 0,
    Live = 1,
    Dev = 2
};

Timestamp_t getUTCTimestamp();

bool syncSysTime(NimBLEAttValue& value);
Mode setMode();

class BleServer  {
    private: 

        /* BLE Server Pointer */
        NimBLEServer *pServer = nullptr;
       /* Vital Signs Service Pointer and Characteristics */
        NimBLEService *pVitalsService = nullptr;
            NimBLECharacteristic *pVitals = nullptr;
    
        /* IMUService Pointer and Characteristics */
        NimBLEService *pMotionService = nullptr;
            NimBLECharacteristic *pRaw = nullptr;
            NimBLECharacteristic *pActivity = nullptr;
            NimBLECharacteristic *pMode = nullptr;

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

            BleAccel_t& operator=(const bno08x_accel_t& accel) {
                x = accel.x;
                y = accel.y;
                z = accel.z;
                accuracy = static_cast<uint8_t>(accel.accuracy);
                return *this;
            }
        }__attribute__((packed));

        BleAccel_t _accel;

        /* Gyro Struct */ 
        struct BleGyro_t {
            float x;
            float y;
            float z;
            uint8_t accuracy;

            BleGyro_t& operator=(const bno08x_gyro_t& gyro) {
                x = gyro.x;
                y = gyro.y;
                z = gyro.z;
                accuracy = static_cast<uint8_t>(gyro.accuracy);
                return *this;
            }
        }__attribute__((packed));

        BleGyro_t _gyro;

        /* Magf Struct */ 
        struct BleMagf_t {
            float x;
            float y;
            float z;
            uint8_t accuracy;

            BleMagf_t& operator=(const bno08x_magf_t& magf) {
                x = magf.x;
                y = magf.y;
                z = magf.z;
                accuracy = static_cast<uint8_t>(magf.accuracy);
                return *this;
            }
        }__attribute__((packed));

        BleMagf_t _magf;

        struct BleRV_t {
            float real;
            float x;
            float y;
            float z ;
            float rad_accuracy;
            uint8_t accuracy;

            BleRV_t& operator=(const bno08x_quat_t& rv) {
                real = rv.real;
                x = rv.i;
                y = rv.j;
                z = rv.k;
                rad_accuracy = rv.rad_accuracy;
                accuracy = static_cast<uint8_t>(rv.accuracy);
                return *this;
            }
        }__attribute__((packed));

        BleRV_t _rv;

        /* Raw Aggregated Struct */ 
        struct BleRaw_t {
            BleAccel_t accel;
            BleGyro_t gyro;
            BleMagf_t magf;
            BleRV_t rv;
            Timestamp_t timestamp;
        }__attribute__((packed));

        BleRaw_t _raw;

        /* Step Count Struct */ 
        struct BleStepCount_t {
            uint32_t latency;
            uint16_t steps;
            uint8_t accuracy;
            

            BleStepCount_t& operator=(const bno08x_step_counter_t& stepCount) {
                latency = stepCount.latency;
                steps = stepCount.steps;
                accuracy = static_cast<uint8_t>(stepCount.accuracy);
                return *this;
            }
        }__attribute__((packed));

        BleStepCount_t _stepCount;

        /* Activity Classifier Struct TODO.... */ 
        struct BleActivityClass_t {
            uint8_t confidence[10];
            uint8_t mostLikelyState;
            uint8_t accuracy;
            

            BleActivityClass_t& operator=(const bno08x_activity_classifier_t& activity_class) {
                for(int i = 0; i < 10; i++) {
                    confidence[i] = activity_class.confidence[i];
                };
                mostLikelyState = static_cast<uint8_t> (activity_class.mostLikelyState);
                accuracy = static_cast<uint8_t>(activity_class.accuracy);
                return *this;
            }

        }__attribute__((packed));

        BleActivityClass_t _activityClass;

        /* Activity Aggregated Struct */ 
        struct BleActivity_t {
            BleStepCount_t stepCount;
            BleActivityClass_t activityClass;
            Timestamp_t timestamp;

        }__attribute__((packed));

        BleActivity_t _activity;
        
        /* Battery Level Status Struct */
        struct BatteryLevel_t {
            uint8_t flags = 0x06; //Battery Level and Additional Status bits set
            uint16_t power_state = 0x0001; //Default 1: Battery Present
            uint8_t battery_level = 0x00;
            uint8_t additional_status = 0x00;
        }__attribute__((packed));

        BatteryLevel_t _batteryLevel;

        /* Battery Energy Status Struct */
        struct BatteryEnergy_t {
            uint8_t flags = 0x01; //Terminal voltage bit set
            uint16_t curr_voltage = 0x0000; //medfloat16 let client interpret
        }__attribute__((packed));

        BatteryEnergy_t _batteryEnergy;

        /* Battery Time Status Struct */
        struct BatteryTime_t {
            uint8_t flags = 0x03; //Time until recharged bit set
            uint8_t time_to_discharge[3] = {0x00, 0x00, 0x00}; // uint8_t[3] to represent 24 bits
            uint8_t time_to_recharge[3] = {0x00, 0x00, 0x00}; // uint8_t[3] to represent 24 bits
        }__attribute__((packed));

        BatteryTime_t _batteryTime;

        /* Battery Health Status Struct */
        struct BatteryHealth_t {
            uint8_t flags = 0x03; //Summary and current temp bits set
            uint8_t health_summary = 0x00;
            int8_t current_temp = 0x00;
        }__attribute__((packed));

        BatteryHealth_t _batteryHealth;

        std::atomic<Mode> _mode{Background};
        std::atomic<bool> _authenticated{false};

        public:
            bool hasSubscriber() { return pServer && pServer->getConnectedCount() > 0; }
            bool isAuthenticated() { return hasSubscriber() && _authenticated; }
            void setAuthenticated(bool auth) { _authenticated = auth; }
            bool init(int8_t tx_power);
            bool deinit();
            bool restart();
            bool startAdvertising();
            bool isAdvertising();
            void setTXPower(int8_t tx_power);

            /* Vitals Characteristic Setters */
            void setHR(bool notify = true);
            void setBR(bool notify = true);

            /* Activity Characteristic Setters */
            void updateAccel(const bno08x_accel_t& accel);
            void updateGyro(const bno08x_gyro_t& gyro);
            void updateMagf(const bno08x_magf_t& magf);
            void updateRV(const bno08x_quat_t& rv_quat);
            void updateStepCount(const bno08x_step_counter_t& step_count) ;
            void updateActivityClass(const bno08x_activity_classifier_t& activity_class);
            void setRaw(bool notify = true);
            void setActivity(bool notify = true);
            
            void setMode(Mode mode);
            Mode getMode();

            /* Environmental Sensor Characteristic Setters */
            void setTemp(bool notify = true); //sint16_t temperature resolution: 0.1°C
            void setHumidity(bool notify = true); //uint16_t humidity resolution: 0.01%

            /** 
            Battery Characteristic Value Setters
            @param  notify - Set NimBLE to notify on this charcteristic
            @param indicate - Set NimBLE to indicate on this charcteristic
            */
           void setBattLevelValue(bool notify = false, bool indicate = true);
           void setBattEnergyValue(bool notify = false, bool indicate = true);
           void setBattTimeValue(bool notify = false, bool indicate = true);
           void setBattHealthValue(bool notify = false, bool indicate = true);

            /* Battery Level Status Functions */
            void updatePowerState(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault);

            /* Battery Level Setter */
            void updateBatteryLevel(uint8_t battery_level);

            /* Additional Status Field Bits Setter*/
            void updateAdditionalStatus(uint8_t service_req, uint8_t batt_fault);

            /* Present Terminal Voltage Stter*/
            void updateCurrVoltage(uint16_t curr_voltage);

            /*Battery Time Status Setters*/
            void updateTimeDischarge(uint8_t time_to_discharge[3]);
            void updateTimeRecharge(uint8_t time_to_recharge[3]);

            /* Battery Health Status Setter */
            void updateHealthSummary(uint8_t health_summary);
            void updateCurrentTemp(int8_t current_temp);


       



            
            

};

extern BleServer bleServer;
extern EventGroupHandle_t bleEventGroup;

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