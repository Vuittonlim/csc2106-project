#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFi.h>

const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttClientId = "M5StickC_Sensor";

const char* topicSound = "sit/canteen/sensor/sound";
const char* topicBLE   = "sit/canteen/sensor/ble";
const char* topicCrowd = "sit/canteen/crowd/index";
const char* topicStatus = "sit/canteen/status/m5stick";

extern String currentSoundLevel;
extern String currentCrowdLabel;
extern int bleCount;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(mqttClientId, topicStatus, 1, true, "offline")) {
      Serial.println("connected!");
      mqttClient.publish(topicStatus, "online", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

void setupMQTT() {
  mqttClient.setServer(mqttServer, mqttPort);
  Serial.println("MQTT initialised");
}

void handleMQTT(String soundLevel, int bleCount, String crowdLabel) {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // QoS 0 — sound (continuous, loss acceptable)
  unsigned long t0 = millis();
  mqttClient.publish(topicSound, soundLevel.c_str(), false);
  Serial.print("QoS 0 latency: "); Serial.print(millis() - t0); Serial.println("ms");

  // QoS 0 — BLE count
  mqttClient.publish(topicBLE, String(bleCount).c_str(), false);

  // QoS 0 + retain — crowd index (best available with PubSubClient)
  unsigned long t1 = millis();
  mqttClient.publish(topicCrowd, crowdLabel.c_str(), true);
  Serial.print("Retained publish latency: "); Serial.print(millis() - t1); Serial.println("ms");

  Serial.println("MQTT published: " + soundLevel + " | " + String(bleCount) + " | " + crowdLabel);
}

#endif
