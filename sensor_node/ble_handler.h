#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>

#define SCAN_DURATION 2
#define BLE_SCAN_INTERVAL 30000

#define BLE_SMOOTH_SIZE 3
int bleHistory[BLE_SMOOTH_SIZE] = {0};
int bleSmootIndex = 0;

int getSmoothedBLE(int newCount) {
  bleHistory[bleSmootIndex] = newCount;
  bleSmootIndex = (bleSmootIndex + 1) % BLE_SMOOTH_SIZE;
  int sum = 0;
  for (int i = 0; i < BLE_SMOOTH_SIZE; i++) sum += bleHistory[i];
  return sum / BLE_SMOOTH_SIZE;
}

extern int bleCount;
unsigned long lastBLEScan = 0;
BLEScan* pBLEScan;

class MyBLECallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == "M5Crowd") return;
    int rssi = advertisedDevice.getRSSI();
    if (rssi > -70) {
      bleCount++;
      Serial.print("  Counted | RSSI: ");
      Serial.print(rssi);
      Serial.print(" | MAC: ");
      Serial.println(advertisedDevice.getAddress().toString().c_str());
    } else {
      Serial.print("  Ignored (too far) | RSSI: ");
      Serial.println(rssi);
    }
  }
};

void setupBLE() {
  BLEDevice::init("M5Crowd");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->setAdvertisedDeviceCallbacks(new MyBLECallbacks());
  Serial.println("BLE initialised");
}

void scanBLE() {
  unsigned long now = millis();
  if (now - lastBLEScan >= BLE_SCAN_INTERVAL || lastBLEScan == 0) {
    lastBLEScan = now;
    bleCount = 0;
    Serial.println("Running BLE scan...");
    BLEScanResults* results = pBLEScan->start(SCAN_DURATION, false);
    bleCount = getSmoothedBLE(bleCount);
    pBLEScan->clearResults();
    Serial.print("BLE devices in range: "); Serial.println(bleCount);
  }
}

void advertiseBLE(String soundLevel, int bleCount, String crowdLabel, int confidence) {
  String payload = "{\"s\":\"" + soundLevel.substring(0,1) +
                   "\",\"b\":" + String(bleCount) +
                   ",\"c\":\"" + crowdLabel.substring(0,1) + "\"}" +
                   "\",\"q\":" + String(confidence) + "}";
  Serial.print("Broadcasting: "); Serial.println(payload);

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setName("M5Crowd");
  String manfData = "";
  manfData += (char)0xFF;
  manfData += (char)0xFF;
  manfData += payload;
  advData.setManufacturerData(manfData);
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();
  delay(1000);
  pAdvertising->stop();
  delay(100);
}

#endif
