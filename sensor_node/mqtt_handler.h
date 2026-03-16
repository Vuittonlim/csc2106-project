#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFi.h>

const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttClientId = "M5StickC_Sensor";

const char* topicZone   = "sit/canteen/zone/seating_2";
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

void handleMQTT(String soundLevel, int bleCount, String crowdLabel, int confidence) {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  String payload = "{\"zone\":\"seating_2\""
                   ",\"s\":\"" + soundLevel.substring(0,1) + "\""
                   ",\"b\":" + String(bleCount) +
                   ",\"c\":\"" + crowdLabel.substring(0,1) + "\""
                   ",\"q\":" + String(confidence) + "}";

  mqttClient.publish(topicZone, payload.c_str(), true);
  Serial.println("MQTT published: " + payload);
}

#endif
