#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

// WiFi & MQTT config -
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER   "broker.hivemq.com"  
#define MQTT_PORT     1883
#define MQTT_TOPIC_SOUND  "sit/canteen/sound"
#define MQTT_TOPIC_BLE    "sit/canteen/ble"
#define MQTT_TOPIC_INDEX  "sit/canteen/crowd_index"


WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

int16_t buffer[256];
String lastLevel = "";
int bleCount = 0;
BLEScan* pBLEScan;

// Crowd Index Fusion
// Returns 0=Low, 1=Medium, 2=High
int getCrowdIndex(String soundLevel, int bleCount) {
  int soundScore = 0;
  if (soundLevel == "Medium") soundScore = 1;
  else if (soundLevel == "High") soundScore = 2;

  int bleScore = 0;
  if (bleCount >= 5 && bleCount < 15) bleScore = 1;
  else if (bleCount >= 15) bleScore = 2;

  // Average both scores and round
  int combined = (soundScore + bleScore);
  if (combined <= 1) return 0;       // Low
  else if (combined <= 3) return 1;  // Medium
  else return 2;                     // High
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    bleCount++;
  }
};

void connectWiFi() {
  M5.Lcd.println("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.println("\nWiFi OK!");
}

void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  while (!mqtt.connected()) {
    M5.Lcd.println("Connecting MQTT...");
    String clientId = "M5Stick-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      M5.Lcd.println("MQTT OK!");
    } else {
      delay(2000);
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Mic.begin();

  connectWiFi();
  connectMQTT();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  M5.Lcd.fillScreen(BLACK);
}

void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  // --- Sound level ---
  if (M5.Mic.record(buffer, 256, 16000)) {
    int16_t peak = 0;
    for (int i = 0; i < 256; i++) {
      if (abs(buffer[i]) > peak) peak = abs(buffer[i]);
    }

    String soundLevel;
    if (peak > 10000) soundLevel = "High";
    else if (peak > 3000) soundLevel = "Medium";
    else soundLevel = "Low";

    // --- BLE scan every 10 seconds ---
    static unsigned long lastBLEScan = 0;
    if (millis() - lastBLEScan > 10000) {
      lastBLEScan = millis();
      bleCount = 0;
      pBLEScan->start(3, false);
      pBLEScan->clearResults();

      mqtt.publish(MQTT_TOPIC_BLE,
        ("{\"ble_count\":" + String(bleCount) + "}").c_str());
    }

    // Compute unified crowd index 
    int crowdIndex = getCrowdIndex(soundLevel, bleCount);
    String crowdLabel;
    uint16_t crowdColor;
    if (crowdIndex == 0) { crowdLabel = "Low";    crowdColor = GREEN; }
    else if (crowdIndex == 1) { crowdLabel = "Medium"; crowdColor = YELLOW; }
    else {                      crowdLabel = "High";   crowdColor = RED; }

    // Update display 
    if (crowdLabel != lastLevel) {
      lastLevel = crowdLabel;
      M5.Lcd.fillScreen(BLACK);

      M5.Lcd.setCursor(10, 5);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.print("Sound: ");
      M5.Lcd.println(soundLevel);

      M5.Lcd.setCursor(10, 30);
      M5.Lcd.print("BLE: ");
      M5.Lcd.println(bleCount);

      M5.Lcd.setCursor(10, 60);
      M5.Lcd.setTextColor(crowdColor);
      M5.Lcd.print("Crowd: ");
      M5.Lcd.println(crowdLabel);

      // Publish all to MQTT
      mqtt.publish(MQTT_TOPIC_SOUND,
        ("{\"level\":\"" + soundLevel + "\",\"peak\":" + String(peak) + "}").c_str());

      mqtt.publish(MQTT_TOPIC_INDEX,
        ("{\"crowd\":\"" + crowdLabel + "\",\"sound\":\"" + soundLevel + "\",\"ble\":" + String(bleCount) + "}").c_str());
    }

    // Sound bar
    M5.Lcd.fillRect(10, 90, 220, 15, BLACK);
    int barWidth = map(peak, 0, 32767, 0, 220);
    M5.Lcd.fillRect(10, 90, barWidth, 15, crowdColor);
  }

  delay(100);
}