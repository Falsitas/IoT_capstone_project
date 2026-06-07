#ifndef __CUSTOM_BLE_H__
#define __CUSTOM_BLE_H__

#include <Arduino.h>
#include <NimBLEDevice.h>

// =====================
// BLE UUID
// =====================
#define BLE_DEVICE_NAME "ESP32_SH"
#define BLE_SERVICE_UUID "781a78cd-654f-4ad9-abe1-42e87f1d537c"
#define BLE_CHAR_UUID "2169484a-848b-4ab4-8d3b-6e972c866112"

class CustomBLE : public NimBLECharacteristicCallbacks {
public:
    CustomBLE();

    void begin();

    bool recStartRequested = false;
    bool recStopRequested = false;
    bool playStopRequested = false;

protected:
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

private:
    NimBLEServer* pServer;
    NimBLEService* pService;
    NimBLECharacteristic* pCharacteristic;
};

#endif