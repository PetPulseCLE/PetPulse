#include <stdio.h>
#include "BNO08x.hpp"
#include "imu_driver.hpp"
#include "esp_rom_sys.h"
#include "NimBLEDevice.h"

extern "C" void app_main(void) {

    NimBLEDevice::init("PetPulse");
    
    NimBLEServer *pServer = NimBLEDevice::createServer();
    NimBLEService *pService = pServer->createService("ABCD");
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic("1234");
    
    pService->start();
    pCharacteristic->setValue("Hello BLE");
    
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("ABCD"); // advertise the UUID of our service
    pAdvertising->setName("PetPulse"); // advertise the device name
    pAdvertising->start(); 

}