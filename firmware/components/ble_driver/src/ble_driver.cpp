#include <stdio.h>
#include "NimBLEAdvertising.h"
#include "NimBLEDevice.h"
#include "NimBLEConnInfo.h"
#include "esp_rom_sys.h"
#include "ble_driver.hpp"


BleServer bleServer;

bool BleServer::init() {

    NimBLEDevice::init(DEVICE_NAME);

    /* Request mtu = 512 */
    NimBLEDevice::setMTU(512);

    /* bonding: true, mitm: false, secure connection: true */ 
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    /* Create server*/
    pServer = NimBLEDevice::createServer();

    /* Define services and respective charcteristics*/
    pVitalsService = pServer->createService(VITALS_UUID);
        pHeartRate = pVitalsService->createCharacteristic(HR_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pBreathRate = pVitalsService->createCharacteristic(BR_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY); 

        /*Start the vital signs service*/
        pVitalsService->start();

    pActivityService = pServer->createService(ACTIVITY_UUID);
        pAccel = pActivityService->createCharacteristic(ACCEL_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pGyro = pActivityService->createCharacteristic(GYRO_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pMagf = pActivityService->createCharacteristic(MAGF_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pStepCount = pActivityService->createCharacteristic(STEP_COUNT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pActivityClass = pActivityService->createCharacteristic(ACTIVITY_CLASS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

        /*Start the Activity Service*/
        pActivityService->start();

    pEnviroService = pServer->createService(ENVIRO_UUID);
        pTemp = pEnviroService->createCharacteristic(TEMP_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pHumidity = pEnviroService->createCharacteristic(HUMIDITY_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

        /* Start the environmental sensors service */
        pEnviroService->start();
        

    pBatteryService = pServer->createService(BATTERY_UUID);
        pBatteryLevel = pBatteryService->createCharacteristic(BATTERY_LEVEL_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryEnergy = pBatteryService->createCharacteristic(BATTERY_ENERGY_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryTime = pBatteryService->createCharacteristic(BATTERY_TIME_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryHealth = pBatteryService->createCharacteristic(BATTERY_HEALTH_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);


        /*Start the battery service*/
        pBatteryService->start();
    
    pCurTimeService = pServer->createService(CUR_TIME_SERVICE_UUID);
        pCurTime = pCurTimeService->createCharacteristic(CUR_TIME_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR);

        /*Start the current time service*/
        pCurTimeService->start();

    return true;
}

bool BleServer::deinit() {

    if(!NimBLEDevice::isInitialized()) {
        return false;
    } 

    NimBLEDevice::deinit();
    return true;
}

bool BleServer::startAdvertising() {

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();

    pAdvertising->setName(DEVICE_NAME);

    /* Check if were already advertising*/
    if(!pAdvertising->isAdvertising()) {
        return pAdvertising->start();
    }

    return true;
}

/* Battery Level Struct Setters */
void BleServer::setPowerState(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault) {
    if(wired_ext > 2) return;
    if(charge_state > 3) return;
    if(charge_level > 3) return;
    if(charge_type > 4) return;
    if(charge_fault > 7) return;

    uint16_t newPowerState = _batteryLevel.power_state;

    /* Clear relevant bits and set new values */
    newPowerState &= ~(0x03 << 1);
    newPowerState |= (static_cast<uint16_t>(wired_ext) & 0x03) << 1; //Located at bits 1-2
    newPowerState &= ~(0x03 << 5);
    newPowerState |= (static_cast<uint16_t>(charge_state) & 0x03) << 5; //Located at bits 5-6
    newPowerState &= ~(0x03 << 7);
    newPowerState |= (static_cast<uint16_t>(charge_level) & 0x03) << 7; //Located at bits 7-8
    newPowerState &= ~(0x07 << 9);
    newPowerState |= (static_cast<uint16_t>(charge_type) & 0x07) << 9; //Located at bits 9-11
    newPowerState &= ~(0x07 << 12);
    newPowerState |= (static_cast<uint16_t>(charge_fault) & 0x07) << 12; //Located at bits 12-14
                
    _batteryLevel.power_state = newPowerState;
}

void BleServer::setBatteryLevel(uint8_t battery_level) {
    if(battery_level > 100) return;
    _batteryLevel.battery_level = battery_level;

    }

void BleServer::setAdditionalStatus(uint8_t service_req, uint8_t batt_fault) {
    if(service_req > 2) return;
    if(batt_fault > 1) return;

    uint8_t newAdditionalStatus = _batteryLevel.additional_status;

    newAdditionalStatus &= ~(0x03); 
    newAdditionalStatus |= (service_req & 0x03);
    newAdditionalStatus &= ~(0x03 << 2); 
    newAdditionalStatus |= (batt_fault & 0x01) << 2;


    _batteryLevel.additional_status = newAdditionalStatus;
    
}

/* Battery Energy Struct Setters */
void BleServer::setCurrVoltage(uint16_t curr_voltage) {

    _batteryEnergy.curr_voltage = curr_voltage;

}

/* Battery Time Struct Setters */
void BleServer::setTimeDischarge(uint8_t time_to_discharge[3]) {
    _batteryTime.time_to_discharge[0] = time_to_discharge[0];
    _batteryTime.time_to_discharge[1] = time_to_discharge[1];
    _batteryTime.time_to_discharge[2] = time_to_discharge[2];

}

void BleServer::setTimeRecharge(uint8_t time_to_recharge[3]) {
    _batteryTime.time_to_recharge[0] = time_to_recharge[0];
    _batteryTime.time_to_recharge[1] = time_to_recharge[1];
    _batteryTime.time_to_recharge[2] = time_to_recharge[2];
    
}

/* Battery Health Struct Setters  */
void BleServer::setHealthSummary(uint8_t health_summary) {
    _batteryHealth.health_summary = health_summary;
}

void BleServer::setCurrentTemp(int8_t current_temp) {
    _batteryHealth.current_temp = current_temp;
}


/* Battery Level Charcteristic Value Setters */
void BleServer::setBattLevelValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryLevel->setValue(_batteryLevel);
        pBatteryLevel->indicate();
    }
    if(notify) {
        pBatteryLevel->setValue(_batteryLevel);
        pBatteryLevel->notify();
    }
}

void BleServer::setBattEnergyValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryLevel->setValue(_batteryEnergy);
        pBatteryLevel->indicate();
    }
    if(notify) {
        pBatteryLevel->setValue(_batteryEnergy);
        pBatteryLevel->notify();
    }
}


void BleServer::setBattTimeValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryLevel->setValue(_batteryTime);
        pBatteryLevel->indicate();
    }
    if(notify) {
        pBatteryLevel->setValue(_batteryTime);
        pBatteryLevel->notify();
    }
}


void BleServer::setBattHealthValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryLevel->setValue(_batteryHealth);
        pBatteryLevel->indicate();
    }
    if(notify) {
        pBatteryLevel->setValue(_batteryHealth);
        pBatteryLevel->notify();
    }
}



/* Server Callbacks */
void ServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo) {
    printf("Client address: %s\n", connInfo.getAddress().toString().c_str());

    /**
     *  We can use the connection handle here to ask for different connection parameters.
     *  Args: connection handle, min connection interval, max connection interval
     *  latency, supervision timeout.
     *  Units; Min/Max Intervals: 1.25 millisecond increments.
     *  Latency: number of intervals allowed to skip.
     *  Timeout: 10 millisecond increments.
     */
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
}
void ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    printf("Client disconnected - start advertising\n");
    if(!NimBLEDevice::getAdvertising()->isAdvertising()) {
        NimBLEDevice::startAdvertising();
    }

}

void ServerCallbacks::onAuthenticationComplete(NimBLEConnInfo& connInfo) {


}
/* Characteristic Callbacks */
void CharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {

}




/* =============================================================================================================== */

// bool ble_init() {

//     // Initialize device 
//     NimBLEDevice::init("PetPulse");

//     NimBLEDevice::setMTU(512);


//     // bonding: true, mitm: false, secure connection: true
//     NimBLEDevice::setSecurityAuth(true, false, true);
//     NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
//     NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
//     NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    
//     // Create server
//     NimBLEServer *pServer = NimBLEDevice::createServer();
  
//     // Create vital signs service with charcteristics for heart rate and breath rate
//     NimBLEService *pService = pServer->createService("180D"); // heart rate service
//     // Charcteristisc must be encrypted for bonding 
//     NimBLECharacteristic *pHRChar = pService->createCharacteristic("2A37", NIMBLE_PROPERTY::READ_ENC |  NIMBLE_PROPERTY::NOTIFY);


//     pHRChar->setValue("100");
//     pService->start();
    
//     NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
//     pAdvertising->addServiceUUID("180D"); 
//     pAdvertising->setName("PetPulse"); 
//     pAdvertising->start(); 

//     while (1) {
//         if(!pAdvertising->isAdvertising()) {
//             pAdvertising->start();
//         }
//         std::vector<uint16_t> clients = pServer->getPeerDevices();

//         if (clients.size() > 0) {
//             esp_rom_printf("Clients: %d\n", clients.size());
//             int i = 0;
//             for (uint16_t client : clients) {
//                 NimBLEConnInfo connInfo = pServer->getPeerInfo(client);
//                 esp_rom_printf("Client Handles %d: %s\n", i, connInfo.isBonded() ? "Bonded" : "Not Bonded");
//             }
//         }
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }

//     return true;
// }
 





