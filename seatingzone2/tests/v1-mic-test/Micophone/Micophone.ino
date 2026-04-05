#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>


#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER   "broker.hivemq.com"  // free public broker
#define MQTT_PORT     1883
#define MQTT_TOPIC    "sit/canteen/sound"   // customise this

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

int16_t buffer[256];
String lastLevel = "";

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

  M5.Lcd.fillScreen(BLACK);
}

void loop() {
  // Keep MQTT connection alive
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (M5.Mic.record(buffer, 256, 16000)) {
    int16_t peak = 0;
    for (int i = 0; i < 256; i++) {
      if (abs(buffer[i]) > peak) peak = abs(buffer[i]);
    }

    String level;
    uint16_t color;
    if (peak > 10000) {
      level = "High";
      color = RED;
    } else if (peak > 3000) {
      level = "Medium";
      color = YELLOW;
    } else {
      level = "Low";
      color = GREEN;
    }

    // Only redraw display if level changed
    if (level != lastLevel) {
      lastLevel = level;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(10, 10);
      M5.Lcd.setTextColor(color);
      M5.Lcd.println(level);

      // Publish to MQTT
      String payload = "{\"level\":\"" + level + "\",\"peak\":" + String(peak) + "}";
      mqtt.publish(MQTT_TOPIC, payload.c_str());
    }

    // Update bar
    M5.Lcd.fillRect(10, 90, 220, 20, BLACK);
    int barWidth = map(peak, 0, 32767, 0, 220);
    M5.Lcd.fillRect(10, 90, barWidth, 20, color);

    delay(100);
  }
}