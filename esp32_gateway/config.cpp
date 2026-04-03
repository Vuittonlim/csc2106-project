// config.h — compile-time configuration for the crowd analytics firmware.
// 
//     Add config.cpp to .gitignore and distribute config.example.cpp instead.
#include "config.h"

// --- Wi-Fi ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";


// --- MQTT broker (HiveMQ Cloud, TLS port 8883) ---
const char* MQTT_BROKER = "a765286b74694e199fc8a5bdefcf0bc1.s1.eu.hivemq.cloud";
const int MQTT_PORT = 8883;

const char* MQTT_USER   = "YOUR_MQTT_USERNAME";
const char* MQTT_PASS   = "YOUR_MQTT_PASSWORD";


// --- Topics ---
const char* TOPIC_SUB = "sit/canteen/zone/#";
const char* TOPIC_PUB = "sit/canteen/analytics";

// --- Timing thresholds ---
const unsigned long MQTT_TIMEOUT        = 10000;  // 10s — MQTT connection health
const unsigned long BLE_FALLBACK_TIMEOUT = 30000; // 30s — zone 2 BLE fallback