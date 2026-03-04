#include <stdio.h>
#include <sys/_timeval.h>
#include "NimBLEAdvertising.h"
#include "NimBLEDevice.h"
#include "NimBLEConnInfo.h"
#include "NimBLEUUID.h"
#include "esp_log.h"
#include "ble_driver.hpp"
#include <sys/time.h>

static const char *TAG = "BLE";


BleServer bleServer;
ServerCallbacks serverCallbacks;
CharacteristicCallbacks chrCallbacks;
EventGroupHandle_t bleEventGroup = nullptr;

bool syncSysTime(NimBLEAttValue& value) {
    if(value.size() < 10) return false;
    struct tm timeinfo = {};
    timeinfo.tm_year = (value[0] | value[1] << 8) - 1900;
    timeinfo.tm_mon = value[2] - 1;
    timeinfo.tm_mday = value[3];
    timeinfo.tm_hour = value[4];
    timeinfo.tm_min = value[5];
    timeinfo.tm_sec = value[6];
    /* Throw out weekday, fraction_ms, and adjust reason*/
    timeinfo.tm_isdst = 0;

    time_t t = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    if(settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "syncSysTime: settimeofday() failed");
        return false;
    }
    xEventGroupSetBits(bleEventGroup, BLE_TIME_SYNCED_BIT);
    return true;
}

Timestamp_t getUTCTimestamp() {
    Timestamp_t ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    gmtime_r(&tv.tv_sec, &tm_info);
    ts.year = tm_info.tm_year + 1900;
    ts.month = tm_info.tm_mon + 1;
    ts.day = tm_info.tm_mday;
    ts.hours = tm_info.tm_hour;
    ts.minutes = tm_info.tm_min;
    ts.seconds = tm_info.tm_sec;
    ts.m_seconds = tv.tv_usec / 1000;
    return ts;
}


/* Server Callbacks */
void ServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo) {
    ESP_LOGI(TAG, "Client address: %s", connInfo.getAddress().toString().c_str());
    /**
     *  We can use the connection handle here to ask for different connection parameters.
     *  Args: connection handle, min connection interval, max connection interval
     *  latency, supervision timeout.
     *  Units; Min/Max Intervals: 1.25 millisecond increments.
     *  Latency: number of intervals allowed to skip.
     *  Timeout: 10 millisecond increments.
     */
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 400);
}
void ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    bleServer.setAuthenticated(false);
    xEventGroupClearBits(bleEventGroup, BLE_AUTHENTICATED_BIT | BLE_TIME_SYNCED_BIT);

    switch(reason) {
        case BLE_HS_ETIMEOUT_HCI:
        case BLE_HS_EOS:
        case BLE_HS_ECONTROLLER:
        case BLE_HS_ENOTSYNCED:
            ESP_LOGW(TAG, "Client disconnected - BLE stack reset (reason=%d), waiting for host re-sync", reason);
            return;
        default:
            ESP_LOGI(TAG, "Client disconnected (reason=%d) - restarting advertising", reason);
            break;
    }

    if(!NimBLEDevice::startAdvertising()) {
        ESP_LOGE(TAG, "Failed to restart advertising after disconnect");
    }
}

void ServerCallbacks::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
    if(!connInfo.isEncrypted()) {
        NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        NimBLEDevice::deleteBond(connInfo.getAddress());
        ESP_LOGW(TAG, "Authentication failed - disconnecting client");
        return;
    }
    ESP_LOGI(TAG, "Authentication successful bonded: %s", connInfo.isBonded() ? "true" : "false");
    bleServer.setAuthenticated(true);
    xEventGroupSetBits(bleEventGroup, BLE_AUTHENTICATED_BIT);
}

/* Characteristic Callbacks */
void CharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    if(pCharacteristic->getUUID() == NimBLEUUID(CUR_TIME_UUID)) {
        if(!connInfo.isEncrypted()) {
            ESP_LOGW(TAG, "onWrite: rejecting time write from unencrypted client");
            return;
        }
        NimBLEAttValue value = pCharacteristic->getValue();
        if(value.size() < 10) {
            ESP_LOGW(TAG, "onWrite: value too short (%d bytes)", value.size());
            return;
        }
        if(!syncSysTime(value)) {
            ESP_LOGW(TAG, "onWrite: failed to sync system time");
            return;
        }

        // Read back to verify
        struct timeval tv_check;
        gettimeofday(&tv_check, NULL);
        struct tm tm_check;
        localtime_r(&tv_check.tv_sec, &tm_check);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_check);
        ESP_LOGI(TAG, "System time set to: %s", buf);
    }
    if(pCharacteristic->getUUID() == NimBLEUUID(C_MODE_UUID)) {
        if(!connInfo.isEncrypted()) {
            ESP_LOGW(TAG, "onWrite: rejecting mode write from unencrypted client");
            return;
        }
        NimBLEAttValue value = pCharacteristic->getValue();
        if(value.size() != 1) {
            ESP_LOGW(TAG, "onWrite: mode value wrong size (%d bytes)", value.size());
            return;
        }
        if(value[0] > 2) {
            ESP_LOGW(TAG, "onWrite: invalid mode value (%d), must be 0-2", value[0]);
            return;
        }
        bleServer.setMode(static_cast<Mode>(value[0]));
    }
}

bool BleServer::init(int8_t tx_power) {

    if(NimBLEDevice::isInitialized()) {
        ESP_LOGW(TAG, "BLE device already initialized");
        return false;
    }

    NimBLEDevice::init(DEVICE_NAME);
    bleEventGroup = xEventGroupCreate();
    if(bleEventGroup == nullptr) {
        ESP_LOGE(TAG, "Failed to create BLE event group");
        NimBLEDevice::deinit();
        return false;
    }

    /* Request mtu = 512 */
    NimBLEDevice::setMTU(512);

    NimBLEDevice::setPower(tx_power);

    /* bonding: true, mitm: false, secure connection: true */ 
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    /* Create server*/
    pServer = NimBLEDevice::createServer();

    if(pServer == nullptr) {
        ESP_LOGE(TAG, "Failed to create server");
        NimBLEDevice::deinit();
        return false;
    }
    pServer->setCallbacks(&serverCallbacks); 

    /* Define services and respective charcteristics*/
    pVitalsService = pServer->createService(S_VITALS_UUID);
        pVitals = pVitalsService->createCharacteristic(C_VITALS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);


        /*Start the vital signs service*/
        pVitalsService->start();

    pMotionService = pServer->createService(S_MOTION_UUID);
        pRaw = pMotionService->createCharacteristic(C_RAW_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pActivity = pMotionService->createCharacteristic(C_ACTIVITY_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pMode = pMotionService->createCharacteristic(C_MODE_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
        pMode->setCallbacks(&chrCallbacks);

        /*Start the Activity Service*/
        pMotionService->start();

    pEnviroService = pServer->createService(S_ENVIRO_UUID);
        pTemp = pEnviroService->createCharacteristic(C_TEMP_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pHumidity = pEnviroService->createCharacteristic(C_HUMIDITY_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

        /* Start the environmental sensors service */
        pEnviroService->start();
        

    pBatteryService = pServer->createService(S_BATTERY_UUID);
        pBatteryLevel = pBatteryService->createCharacteristic(BATTERY_LEVEL_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryEnergy = pBatteryService->createCharacteristic(BATTERY_ENERGY_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryTime = pBatteryService->createCharacteristic(BATTERY_TIME_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
        pBatteryHealth = pBatteryService->createCharacteristic(BATTERY_HEALTH_STAT_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);


        /*Start the battery service*/
        pBatteryService->start();
    
    pCurTimeService = pServer->createService(S_CUR_TIME_SERVICE_UUID);
        pCurTime = pCurTimeService->createCharacteristic(CUR_TIME_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE);
        pCurTime->setCallbacks(&chrCallbacks);

        /*Start the current time service*/
        pCurTimeService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(DEVICE_NAME);
    pAdvertising->addServiceUUID(S_BATTERY_UUID);
    pAdvertising->addServiceUUID(S_CUR_TIME_SERVICE_UUID);

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
    /* Check if were already advertising*/
    if(!pAdvertising->isAdvertising()) {
        return pAdvertising->start();
    }

    return true;
}

bool BleServer::isAdvertising() {
    return NimBLEDevice::getAdvertising()->isAdvertising();
}
 
void BleServer::updateAccel(const bno08x_accel_t& accel) {
    _accel = accel;

}
void BleServer::updateGyro(const bno08x_gyro_t& gyro) {
    _gyro = gyro;
}
void BleServer::updateMagf(const bno08x_magf_t& magf) {
    _magf = magf;
}

void BleServer::updateRV(const bno08x_quat_t& rv_quat) {
    _rv = rv_quat;
}
void BleServer::updateStepCount(const bno08x_step_counter_t& step_count) {
    _stepCount = step_count;

}
void BleServer::updateActivityClass(const bno08x_activity_classifier_t& activity_class) {
    _activityClass = activity_class;
}

void BleServer::setRaw(bool notify) {
    _raw = {
        .accel = _accel,
        .gyro = _gyro,
        .magf = _magf,
        .rv = _rv,
        .timestamp = getUTCTimestamp(),
    };
    if(notify) {
        pRaw->setValue(_raw);
        pRaw->notify();
    }
}

void BleServer::setActivity(bool notify) {
    _activity = {
        .stepCount = _stepCount,
        .activityClass = _activityClass,
        .timestamp = getUTCTimestamp(),
    };
    if(notify) {
        pActivity->setValue(_activity);
        pActivity->notify();
    }
}


void BleServer::setMode(Mode mode) {
    _mode = mode;
    uint8_t val = static_cast<uint8_t>(mode);
    pMode->setValue(&val, 1);
    ESP_LOGI(TAG, "Mode set to: %d", val);
}

Mode BleServer::getMode() {
    return _mode;
}

/* Battery Level Characteristic Value Setters */
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
        pBatteryEnergy->setValue(_batteryEnergy);
        pBatteryEnergy->indicate();
    }
    if(notify) {
        pBatteryEnergy->setValue(_batteryEnergy);
        pBatteryEnergy->notify();
    }
}


void BleServer::setBattTimeValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryTime->setValue(_batteryTime);
        pBatteryTime->indicate();
    }
    if(notify) {
        pBatteryTime->setValue(_batteryTime);
        pBatteryTime->notify();
    }
}


void BleServer::setBattHealthValue(bool notify, bool indicate) {
    if(indicate) {
        pBatteryHealth->setValue(_batteryHealth);
        pBatteryHealth->indicate();
    }
    if(notify) {
        pBatteryHealth->setValue(_batteryHealth);
        pBatteryHealth->notify();
    }
}
    

/* Battery Structs Update Fields */
void BleServer::updatePowerState(uint8_t wired_ext, uint8_t charge_state, uint8_t charge_level, uint8_t charge_type, uint8_t charge_fault) {
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

void BleServer::updateBatteryLevel(uint8_t battery_level) {
    if(battery_level > 100) return;
    _batteryLevel.battery_level = battery_level;

    }

void BleServer::updateAdditionalStatus(uint8_t service_req, uint8_t batt_fault) {
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
void BleServer::updateCurrVoltage(uint16_t curr_voltage) {

    _batteryEnergy.curr_voltage = curr_voltage;

}

/* Battery Time Struct Setters */
void BleServer::updateTimeDischarge(uint8_t time_to_discharge[3]) {
    _batteryTime.time_to_discharge[0] = time_to_discharge[0];
    _batteryTime.time_to_discharge[1] = time_to_discharge[1];
    _batteryTime.time_to_discharge[2] = time_to_discharge[2];

}

void BleServer::updateTimeRecharge(uint8_t time_to_recharge[3]) {
    _batteryTime.time_to_recharge[0] = time_to_recharge[0];
    _batteryTime.time_to_recharge[1] = time_to_recharge[1];
    _batteryTime.time_to_recharge[2] = time_to_recharge[2];
    
}

/* Battery Health Struct Setters  */
void BleServer::updateHealthSummary(uint8_t health_summary) {
    _batteryHealth.health_summary = health_summary;
}

void BleServer::updateCurrentTemp(int8_t current_temp) {
    _batteryHealth.current_temp = current_temp;
}




