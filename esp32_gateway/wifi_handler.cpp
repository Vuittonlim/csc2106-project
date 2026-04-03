/**
 * @file wifi_handler.cpp
 * @brief Wi-Fi and MQTT connectivity layer for the crowd analytics system.
 *
 * Responsibilities:
 *  - Connects the ESP32 to Wi-Fi and a secure MQTT broker (TLS, no cert validation).
 *  - Subscribes to occupancy sensor topics and routes incoming JSON payloads
 *    into the shared dataQueue as DataPackets for downstream processing.
 *  - Tracks the last MQTT activity timestamp per-zone so ble_handler.cpp can
 *    detect Zone 2 dropout and activate BLE fallback.
 *  - Publishes outbound analytics JSON produced by processing.cpp.
 *  - Auto-reconnects to the broker if the connection drops.
 */
#include "wifi_handler.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "data_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "config.h"

// --- External shared state ---
extern QueueHandle_t dataQueue;
extern bool useFallback;

/// Timestamp (ms) of the most recent MQTT message received on any zone topic
unsigned long lastMQTT       = 0;

/// Timestamp (ms) of the most recent MQTT message received specifically for Zone 2.
/// Monitored by ble_handler.cpp to detect Zone 2 dropout.
unsigned long lastMQTT_zone2 = 0;

// ─────────────────────────
// MQTT CLIENT
// ─────────────────────────
WiFiClientSecure espClient;
PubSubClient client(espClient);


/**
 * @brief Lightweight struct for temporarily holding parsed MQTT fields.
 *
 * Used internally; downstream code receives a DataPacket via the queue instead.
 */
struct MQTTData {
  String zone;     ///< Zone identifier extracted from the topic (e.g. "1", "2")
  int b;          ///< Occupancy count
};

MQTTData mqttData;

/**
 * @brief PubSubClient message callback — parses incoming MQTT JSON and enqueues a DataPacket.
 *
 * Triggered by PubSubClient whenever a message arrives on a subscribed topic.
 * Performs the following steps:
 *  1. Reassembles the raw byte payload into a String.
 *  2. Extracts the zone identifier from the trailing segment of the topic path
 *     (e.g. "sensors/seating_2" → "2").
 *  3. Parses the JSON body, supporting both "b" (legacy) and "ble" as the count key.
 *  4. Constructs a DataPacket and pushes it onto dataQueue.
 *  5. Updates lastMQTT (and lastMQTT_zone2 for Zone 2) for fallback watchdog logic.
 *
 * @param topic   Null-terminated topic string (e.g. "sensors/seating_1").
 * @param payload Raw message bytes (not null-terminated).
 * @param length  Number of bytes in payload.
 */
void callback(char* topic, byte* payload, unsigned int length) {

  String msg = "";

  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("[RAW MQTT] " + msg);

  // Extract zone from topic
  String topicStr = String(topic);
  int lastSlash = topicStr.lastIndexOf('/');
  String zone = topicStr.substring(lastSlash + 1);

  // Normalize zone name: "seating_1" → "1", "seating_2" → "2"
  if (zone.startsWith("seating_")) zone = zone.substring(8);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);

  if (!err) {

    // Support both "b" (legacy) and "ble" field names for count
    int count = doc.containsKey("ble") ? (int)doc["ble"] : (int)doc["b"];
    float temp = doc["t"] | 0.0f;
    bool fallback = doc["fallback"] | false;

    // SEND TO QUEUE
    DataPacket packet;
    strncpy(packet.source, fallback ? "ble" : "mqtt", sizeof(packet.source) - 1);
    packet.source[sizeof(packet.source) - 1] = '\0';
    strncpy(packet.zone, zone.c_str(), sizeof(packet.zone) - 1);
    packet.zone[sizeof(packet.zone) - 1] = '\0';
    packet.count = count;
    packet.temp  = temp;
    packet.payload[0] = '\0';

    if (xQueueSend(dataQueue, &packet, pdMS_TO_TICKS(200)) != pdPASS) {
      Serial.println("[MQTT] Queue full — packet dropped for zone " + zone);
    } else {
      Serial.println("[MQTT → QUEUE] " + zone + " = " + String(count));
    }

  } else {
    Serial.println("[MQTT] JSON parse failed");
  }

  lastMQTT = millis();
  if (zone == "2") lastMQTT_zone2 = millis();
}


/**
 * @brief Connect to Wi-Fi and the MQTT broker, then subscribe to sensor topics.
 *
 * Blocks until Wi-Fi is established. TLS certificate validation is disabled
 * (setInsecure) — suitable for development; consider proper cert pinning for production.
 * Must be called once at startup before mqttTask() is started.
 */
void setupMQTT() {

  Serial.println("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");

  espClient.setInsecure();

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.connect("ESP32Client", MQTT_USER, MQTT_PASS);

  client.setCallback(callback);
  client.subscribe(TOPIC_SUB);
  Serial.print("[MQTT]: Ready\n");
}

/**
 * @brief Blocking reconnect loop — retries until the MQTT broker accepts the connection.
 *
 * Called automatically by handleMQTT() when the connection is found to be dropped.
 * Waits 2 seconds between attempts to avoid hammering the broker.
 * Re-subscribes to TOPIC_SUB on every successful reconnect.
 */
void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting to HiveMQ...");

    if (client.connect("ESP32Client", MQTT_USER, MQTT_PASS)) {

      Serial.println("connected!");

      client.subscribe(TOPIC_SUB);

    } else {

      Serial.print("failed, rc=");
      Serial.println(client.state());

      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
  }
}


/**
 * @brief Service the MQTT connection — reconnect if needed and process incoming messages.
 *
 * Must be called frequently (every ~50 ms) to keep the PubSubClient state machine
 * ticking and to ensure the callback fires promptly for incoming messages.
 */
void handleMQTT() {

  if (!client.connected()) {
    reconnect();
  }

  client.loop();
}

/**
 * @brief FreeRTOS task wrapper for handleMQTT().
 *
 * Runs handleMQTT() every 50 ms. The short interval ensures MQTT keep-alives
 * are sent on time and incoming packets are delivered to the callback with
 * minimal latency.
 *
 * @param pvParameters Unused (required by FreeRTOS task signature).
 */
void mqttTask(void *pvParameters) {

  while (true) {

    handleMQTT();

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Publish a JSON analytics payload to the outbound MQTT topic.
 *
 * Called by processing.cpp after each analytics computation cycle.
 * Silently skips publishing if the client is not currently connected —
 * the next cycle will attempt again once connectivity is restored.
 *
 * @param payload Serialised JSON string to publish to TOPIC_PUB.
 */
void publishAnalytics(String payload) {

  if (client.connected()) {

    client.publish(TOPIC_PUB, payload.c_str());

    Serial.println("[MQTT OUT] " + payload);
  }
}