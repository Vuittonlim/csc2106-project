#ifndef CONFIG_H
#define CONFIG_H

// WiFi
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;

// MQTT
extern const char* MQTT_BROKER;
extern const int MQTT_PORT;

extern const char* MQTT_USER;
extern const char* MQTT_PASS;

// Topics
extern const char* TOPIC_SUB;
extern const char* TOPIC_PUB;

// Timing
extern const unsigned long MQTT_TIMEOUT;
extern const unsigned long BLE_FALLBACK_TIMEOUT;

#endif