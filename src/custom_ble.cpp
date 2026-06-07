#include "custom_ble.h"

CustomBLE::CustomBLE() : pServer(nullptr), pService(nullptr),pCharacteristic(nullptr) {

}

void CustomBLE::begin() {
  NimBLEDevice::init(BLE_DEVICE_NAME);

  pServer = NimBLEDevice::createServer();

  pService = pServer->createService(BLE_SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(BLE_CHAR_UUID, NIMBLE_PROPERTY::WRITE);

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

  if (cmd == "REC_START") {
      recStartRequested = true;
  }
  else if (cmd == "REC_STOP") {
      recStopRequested = true;
  }
  else if (cmd == "PLAY_STOP") {
      playStopRequested = true;
  }
}