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

// ─────────────────────────
// EXTERNALS
// ─────────────────────────
extern QueueHandle_t dataQueue;
extern bool useFallback;
unsigned long lastMQTT = 0;

// ─────────────────────────
// MQTT CLIENT
// ─────────────────────────
WiFiClientSecure espClient;
PubSubClient client(espClient);


// ─────────────────────────
// DATA STRUCT
// ─────────────────────────
struct MQTTData {
  String zone;
  int b;
};

MQTTData mqttData;

// ─────────────────────────
// CALLBACK (RAW → QUEUE)
// ─────────────────────────
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

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, msg);

  if (!err) {

    int count = doc["b"];
    String crowd = doc["c"];
    int confidence = doc["q"];
    bool fallback = doc["fallback"] | false;

    // SEND TO QUEUE
    DataPacket packet;
    packet.source = fallback ? "ble" : "mqtt";
    packet.zone = zone;
    packet.count = count;

    xQueueSend(dataQueue, &packet, portMAX_DELAY);

    Serial.println("[MQTT → QUEUE] " + zone + " = " + String(count));

  } else {
    Serial.println("[MQTT] JSON parse failed");
  }

  lastMQTT = millis();
  useFallback = false;
}


// ─────────────────────────
// SETUP
// ─────────────────────────
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
  Serial.print("[MQTT]: Ready");
}

// ─────────────────────────
// RECONNECT
// ─────────────────────────
void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting to HiveMQ...");

    if (client.connect("ESP32Client", MQTT_USER, MQTT_PASS)) {

      Serial.println("connected!");

      client.subscribe(TOPIC_SUB);

    } else {

      Serial.print("failed, rc=");
      Serial.println(client.state());

      delay(2000);
    }
  }
}


// ─────────────────────────
// LOOP HANDLER
// ─────────────────────────
void handleMQTT() {

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  if (millis() - lastMQTT > MQTT_TIMEOUT) {
    useFallback = true;
  }
}

// ─────────────────────────
// TASK
// ─────────────────────────
void mqttTask(void *pvParameters) {

  while (true) {

    handleMQTT();

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ─────────────────────────
// PUBLISH FUNCTION (USED BY processor.cpp)
// ─────────────────────────
void publishAnalytics(String payload) {

  if (client.connected()) {

    client.publish(TOPIC_PUB, payload.c_str());

    Serial.println("[MQTT OUT] " + payload);
  }
}