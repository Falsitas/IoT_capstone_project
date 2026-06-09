#ifndef __CUSTOM_BLE_H__
#define __CUSTOM_BLE_H__

#include <Arduino.h>
#include <NimBLEDevice.h>

#define BLE_DEVICE_NAME "ESP32_SH"
#define BLE_SERVICE_UUID "781a78cd-654f-4ad9-abe1-42e87f1d537c"
#define BLE_CHAR_UUID "2169484a-848b-4ab4-8d3b-6e972c866112"

class CustomBLE : 
    public NimBLECharacteristicCallbacks,
    public NimBLEServerCallbacks
{
public:
    CustomBLE();

    void begin();

    bool recRequested = false;
    bool playStopRequested = false;

protected:
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;

private:
    NimBLEServer* pServer;
    NimBLEService* pService;
    NimBLECharacteristic* pCharacteristic;
};

#endif