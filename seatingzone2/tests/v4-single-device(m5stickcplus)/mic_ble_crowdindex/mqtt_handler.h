#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFiClientSecure.h>


#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER   "broker.hivemq.com"  
#define MQTT_PORT     1883

const char* topicZone   = "sit/canteen/zone/seating_2";
const char* topicStatus = "sit/canteen/status/m5stick";

extern String currentSoundLevel;
extern String currentCrowdLabel;

WiFiClientSecure espClient;
PubSubClient     mqttClient(espClient);

void connectMQTT() {
  espClient.setInsecure();
  espClient.setTimeout(15);
  mqttClient.setBufferSize(512);

  Serial.print("Free heap before TLS: ");
  Serial.println(ESP.getFreeHeap());

  if (!espClient.connect(mqttServer, mqttPort)) {
    Serial.println("TCP/TLS failed");
    return;
  }

  while (!mqttClient.connected()) {
    Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(mqttClientId, mqttUser, mqttPassword,
                           topicStatus, 1, true, "offline")) {
      Serial.println("connected!");
      mqttClient.publish(topicStatus, "online", true);
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void setupMQTT() {
  mqttClient.setServer(mqttServer, mqttPort);
  Serial.println("MQTT initialised");
}

void handleMQTT(String soundLevel, String crowdLabel) {
  if (!mqttClient.connected()) {
    connectMQTT();
    if (!mqttClient.connected()) return;
  }
  mqttClient.loop();

  String payload = "{\"zone\":\"seating_2\""
                   ",\"s\":\"" + soundLevel.substring(0, 1) + "\""
                   ",\"c\":\"" + crowdLabel.substring(0, 1) + "\"}";

  if (mqttClient.publish(topicZone, payload.c_str(), true)) {
    Serial.println("MQTT published: " + payload);
  } else {
    Serial.println("MQTT publish failed");
  }
}

#endif