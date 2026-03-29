#include "ble_handler.h"
#include "data_types.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern QueueHandle_t dataQueue;
extern bool useFallback;

BLEScan* pBLEScan;
String scanBLE();

struct BLEData {
  String zone;
  String crowd;
  int b;
  int q;
  unsigned long timestamp;
};

BLEData bleData;
bool bleAvailable = false;
bool simulateBLE = true;

// Setup
void setupBLE() {
  Serial.println("[BLE] Initializing...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  x1
  pBLEScan->setActiveScan(true);

  Serial.println("[BLE] Ready");
}

void handleBLE() {

  String raw = "";
  if (!useFallback) return;   // ONLY run when MQTT fails

  Serial.println("[BLE] Fallback active");

  if (raw != "") {

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, raw);

    if (!err) {

      DataPacket packet;
      packet.source = "ble";
      packet.zone = doc["zone"].as<String>();
      packet.count = doc["b"];

      xQueueSend(dataQueue, &packet, portMAX_DELAY);

      Serial.println("[BLE → QUEUE]");
    }
  }
}

void bleTask(void *pvParameters) {
  while (true) {
    handleBLE();
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}