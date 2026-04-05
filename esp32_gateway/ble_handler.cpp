/**
 * @file ble_handler.cpp
 * @brief BLE fallback handler for Zone 2 occupancy data.
 *
 * Monitors Zone 2 MQTT activity. If MQTT goes silent for longer than
 * BLE_FALLBACK_TIMEOUT, this module activates a BLE scan to retrieve
 * occupancy data from a nearby M5Crowd device and enqueues it as a
 * standard DataPacket — keeping the analytics pipeline fed without
 * any change to downstream processing logic.
 */

#include "ble_handler.h"
#include "data_types.h"
#include "config.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// --- External shared state ---
extern QueueHandle_t dataQueue;
extern bool useFallback;
extern unsigned long lastMQTT_zone2;

BLEScan* pBLEScan;

// --- BLE scan result state (written by callback, read by handleBLE) ---
static String m5CrowdPayload = "";  ///< Raw JSON service-data string from M5Crowd advertisement
static bool   m5CrowdFound   = false;  ///< Set to true when M5Crowd is detected during a scan


/**
 * @brief BLE advertised-device callback that targets the "M5Crowd" device.
 *
 * Fires for every device discovered during a scan. If the device name matches
 * "M5Crowd" and it carries service data, the payload is captured and the scan
 * is stopped early to minimise latency.
 */
class M5CrowdCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    if (String(device.getName().c_str()) == "M5Crowd") {
      if (device.haveServiceData()) {
        m5CrowdPayload = String(device.getServiceData().c_str());
        m5CrowdFound = true;
        device.getScan()->stop();
      }
    }
  }
};

/**
 * @brief Initialise the BLE stack and configure scan parameters.
 *
 * Must be called once at startup before any calls to handleBLE() or bleTask().
 * Uses active scanning so devices respond with their full advertisement payload.
 */
void setupBLE() {
  Serial.println("[BLE] Initializing...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new M5CrowdCallback());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  Serial.println("\n[BLE] Ready");
}

/**
 * @brief Check Zone 2 MQTT health and run a BLE scan if fallback is needed.
 *
 * Called periodically from bleTask(). Behaviour:
 *  - If Zone 2 MQTT arrived within BLE_FALLBACK_TIMEOUT → disable fallback and return.
 *  - If Zone 2 MQTT has been silent too long → scan for M5Crowd, parse its JSON
 *    payload, and push a DataPacket onto dataQueue so processing continues normally.
 *
 * The JSON field "b" in the M5Crowd advertisement carries the occupancy count.
 */
void handleBLE() {

  unsigned long elapsed = millis() - lastMQTT_zone2;

  // Zone 2 MQTT still alive — fallback not needed
  if (elapsed < BLE_FALLBACK_TIMEOUT) {
    if (useFallback) {
      useFallback = false;
      Serial.println("\n[BLE] Zone 2 MQTT resumed — BLE fallback disabled");
    }
    return;
  }

  // Zone 2 MQTT has timed out — switch to BLE fallback
  if (!useFallback) {
    useFallback = true;
    Serial.printf("\n[BLE] Zone 2 MQTT silent for %lu ms — activating BLE fallback", elapsed);
  }

 // Reset scan result flags before each scan attempt
  m5CrowdFound   = false;
  m5CrowdPayload = "";

 // Scan for up to 3 seconds; M5CrowdCallback will stop it early if the device is found
  Serial.println("\n[BLE] Scanning for M5Crowd...");
  pBLEScan->start(3, false);  
  pBLEScan->clearResults();

  if (!m5CrowdFound) {
    Serial.println("\n[BLE] M5Crowd not found");
    return;
  }

  Serial.println("\n[BLE] M5Crowd found: " + m5CrowdPayload);

  // Parse JSON payload
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, m5CrowdPayload);

  if (err) {
    Serial.println("\n[BLE] JSON parse failed");
    return;
  }

    // "b" is the compact key for the occupancy count broadcast by M5Crowd
  int count = doc["b"] | 0;


  // Build a DataPacket 
  DataPacket packet;
  packet.confidence = 0;
  packet.pir        = 0;
  packet.humidity   = 0.0f;
  packet.sound[0]   = '\0';
  strncpy(packet.source, "ble", sizeof(packet.source) - 1);
  packet.source[sizeof(packet.source) - 1] = '\0';
  strncpy(packet.zone, "2", sizeof(packet.zone) - 1);  // BLE is fallback for zone 2
  packet.zone[sizeof(packet.zone) - 1] = '\0';
  packet.count   = count;
  packet.temp    = 0.0f;
  packet.payload[0] = '\0';

  if (xQueueSend(dataQueue, &packet, pdMS_TO_TICKS(200)) == pdPASS) {
    Serial.printf("[BLE → QUEUE] zone=%s count=%d\n", packet.zone, packet.count);
  } else {
    Serial.println("[BLE] Queue full — packet dropped");
  }
}

/**
 * @brief FreeRTOS task wrapper for handleBLE().
 *
 * Runs handleBLE() every 500 ms, yielding to other tasks in between.
 * The 500 ms cadence means BLE data is refreshed roughly twice per second
 * when fallback is active.
 *
 * @param pvParameters Unused (required by FreeRTOS task signature).
 */
void bleTask(void *pvParameters) {
  while (true) {
    handleBLE();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}
