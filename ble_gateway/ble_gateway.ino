#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid     = "xinsiPhone";
const char* password = "watermelon123";

// MQTT broker
const char* mqttServer   = "broker.hivemq.com";
const int   mqttPort     = 1883;
const char* mqttClientId = "M5Gateway_seating2";
const char* zoneTopic    = "sit/canteen/zone/seating_2";

// If no MQTT message from sensor node for this long, activate fallback
#define SILENCE_THRESHOLD 10000  // 10 seconds 

// BLE
#define SCAN_DURATION     3      // seconds per scan
#define BLE_SCAN_INTERVAL 15000  // scan every 15s

BLEScan* pBLEScan;
String   lastBLEPayload = "";
String   lastCompact    = "";  // compact format for display
bool     bleDataFresh   = false;

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

unsigned long lastMQTTReceived = 0;
unsigned long lastBLEScan      = 0;
bool          fallbackActive   = false;

// Called when a message arrives on the subscribed zone topic
void onMQTTMessage(char* topic, byte* payload, unsigned int length) {
  // Ignore own fallback messages echoed back from the broker
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  if (msg.indexOf("\"fallback\":1") >= 0) return;

  lastMQTTReceived = millis();
  if (fallbackActive) {
    fallbackActive = false;
    Serial.println("Primary node recovered, fallback deactivated");
  }
}

// compact "H|12|H|1" format into JSON string
String parseCompactPayload(String compact) {
  int p1 = compact.indexOf('|');
  int p2 = compact.indexOf('|', p1 + 1);
  int p3 = compact.indexOf('|', p2 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0) return "";

  String s = compact.substring(0, p1);
  String b = compact.substring(p1 + 1, p2);
  String c = compact.substring(p2 + 1, p3);
  String q = compact.substring(p3 + 1);

  return "{\"zone\":\"seating_2\",\"fallback\":1,\"s\":\"" + s +
         "\",\"b\":" + b + ",\"c\":\"" + c + "\",\"q\":" + q + "}";
}

class GatewayBLECallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    if (device.getName() != "M5Crowd") return;
    if (!device.haveManufacturerData()) return;

    String manf = device.getManufacturerData();
    // First 2 bytes are manufacturer ID (0xFF 0xFF), rest is compact payload
    if (manf.length() > 2) {
      String compact = manf.substring(2);
      String parsed  = parseCompactPayload(compact);
      if (parsed.length() > 0) {
        lastBLEPayload = parsed;
        lastCompact    = compact;
        bleDataFresh   = true;
        Serial.println("BLE received: " + compact + " → " + parsed);
      }
    }
  }
};

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(mqttClientId)) {
      mqttClient.subscribe(zoneTopic);
      Serial.println("connected, monitoring " + String(zoneTopic));
    } else {
      Serial.print("failed rc="); Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void updateDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 5);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("BLE Gateway");

  M5.Lcd.setCursor(10, 35);
  if (fallbackActive) {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.println("FALLBACK ON");
    // compact "M|4|L|1" for clean display
    int p1 = lastCompact.indexOf('|');
    int p2 = lastCompact.indexOf('|', p1 + 1);
    int p3 = lastCompact.indexOf('|', p2 + 1);
    if (p1 > 0 && p2 > 0 && p3 > 0) {
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.setCursor(10, 68); M5.Lcd.print("Sound:");
      M5.Lcd.setCursor(75, 68); M5.Lcd.println(lastCompact.substring(0, p1));
      M5.Lcd.setCursor(10, 85); M5.Lcd.print("BLE:");
      M5.Lcd.setCursor(75, 85); M5.Lcd.println(lastCompact.substring(p1 + 1, p2));
      M5.Lcd.setCursor(10, 102); M5.Lcd.print("Crowd:");
      M5.Lcd.setCursor(75, 102); M5.Lcd.println(lastCompact.substring(p2 + 1, p3));
    }
  } else {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.println("STANDBY");
    M5.Lcd.setCursor(10, 65);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.println("Primary node OK");
    unsigned long silentFor = (millis() - lastMQTTReceived) / 1000;
    M5.Lcd.println("Last seen: " + String(silentFor) + "s ago");
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Starting...");
  Serial.begin(115200);

  connectWiFi();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(onMQTTMessage);
  connectMQTT();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new GatewayBLECallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  lastMQTTReceived = millis();  // assume primary is up at start

  M5.Lcd.fillScreen(BLACK);
  Serial.println("Gateway ready");
}

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // Periodic BLE scan
  unsigned long now = millis();
  if (now - lastBLEScan >= BLE_SCAN_INTERVAL || lastBLEScan == 0) {
    lastBLEScan = now;
    Serial.println("Scanning BLE...");
    pBLEScan->start(SCAN_DURATION, false);
    pBLEScan->clearResults();
  }

  // Check if primary node has gone silent
  bool silence = (millis() - lastMQTTReceived) > SILENCE_THRESHOLD;

  if (silence && lastBLEPayload.length() > 0) {
    fallbackActive = true;
    // lastBLEPayload is already full JSON with zone + fallback fields
    mqttClient.publish(zoneTopic, lastBLEPayload.c_str(), true);
    Serial.println("Fallback published: " + lastBLEPayload);
    bleDataFresh = false;
  }

  updateDisplay();
  delay(1000);
}
