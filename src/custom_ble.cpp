#include "custom_ble.h"

CustomBLE::CustomBLE() : pServer(nullptr), pService(nullptr),pCharacteristic(nullptr) {

}

void CustomBLE::begin() {
  NimBLEDevice::init(BLE_DEVICE_NAME);

  pServer = NimBLEDevice::createServer();

  pService = pServer->createService(BLE_SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(BLE_CHAR_UUID, NIMBLE_PROPERTY::WRITE);

  pServer->setCallbacks(this);
  pCharacteristic->setCallbacks(this);

  pService->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();

  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setName(BLE_DEVICE_NAME);
  advertising->enableScanResponse(true);
  advertising->start();
}

void CustomBLE::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) {
  String cmd = characteristic->getValue().c_str();

  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "REC") {
    recRequested = true;
  } else if (cmd == "PLAY") {
    playStopRequested = true;
  }
}

void CustomBLE::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  digitalWrite(LED_BUILTIN, HIGH);
}

void CustomBLE::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  digitalWrite(LED_BUILTIN, LOW);
  NimBLEDevice::startAdvertising();
}