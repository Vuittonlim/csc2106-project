#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>

#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define MQTT_TOPIC    "sit/canteen/latency_test"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
BLEScan* pBLEScan;

unsigned long t1, t2;
int testRuns = 5; // repeat each test 5 times for average

void printDivider() {
  Serial.println("----------------------------------------");
}

//  TEST 1: WiFi Connection Time 
void testWiFiConnectionTime() {
  printDivider();
  Serial.println("TEST 1: WiFi Connection Time");
  printDivider();

  unsigned long total = 0;
  for (int i = 0; i < testRuns; i++) {
    WiFi.mode(WIFI_OFF);
    delay(1000); // full disconnect

    WiFi.mode(WIFI_STA);
    t1 = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) delay(10);
    t2 = millis();

    unsigned long duration = t2 - t1;
    total += duration;
    Serial.print("  Run "); Serial.print(i+1);
    Serial.print(": "); Serial.print(duration); Serial.println(" ms");
  }
  Serial.print("  AVERAGE: "); Serial.print(total / testRuns); Serial.println(" ms");
  Serial.println();
}

// TEST 2: MQTT Publish Latency
void testMQTTPublishLatency() {
  printDivider();
  Serial.println("TEST 2: MQTT Publish Latency");
  printDivider();

  // Connect WiFi first
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(10);
  Serial.println("  WiFi connected");

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  unsigned long total = 0;
  for (int i = 0; i < testRuns; i++) {
    // Connect MQTT
    while (!mqtt.connected()) {
      String clientId = "M5Test-" + String(random(0xffff), HEX);
      mqtt.connect(clientId.c_str());
      delay(100);
    }

    // Measure publish time
    String payload = "{\"test\":\"latency\",\"run\":" + String(i) + "}";
    t1 = micros();
    mqtt.publish(MQTT_TOPIC, payload.c_str());
    t2 = micros();

    unsigned long duration = t2 - t1;
    total += duration;
    Serial.print("  Run "); Serial.print(i+1);
    Serial.print(": "); Serial.print(duration); Serial.println(" us");
    mqtt.loop();
    delay(500);
  }
  Serial.print("  AVERAGE: "); Serial.print(total / testRuns); Serial.println(" us");
  Serial.println();

  mqtt.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(500);
}

// TEST 3: BLE Scan Duration vs Devices Found
void testBLEScanLatency() {
  printDivider();
  Serial.println("TEST 3: BLE Scan Duration vs Devices Found");
  printDivider();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  int scanDurations[] = {1, 2, 3, 5}; // seconds
  for (int d = 0; d < 4; d++) {
    int duration = scanDurations[d];
    unsigned long total = 0;
    int totalDevices = 0;

    for (int i = 0; i < 3; i++) { // 3 runs per duration
      pBLEScan->clearResults();
      t1 = millis();
      BLEScanResults* results = pBLEScan->start(duration, false);
      t2 = millis();

      int devices = results->getCount();
      unsigned long elapsed = t2 - t1;
      total += elapsed;
      totalDevices += devices;
      pBLEScan->clearResults();
      delay(500);
    }

    Serial.print("  Scan duration "); Serial.print(duration);
    Serial.print("s -> Avg time: "); Serial.print(total / 3);
    Serial.print(" ms | Avg devices: "); Serial.println(totalDevices / 3);
  }
  Serial.println();

  BLEDevice::deinit(true);
  delay(300);
}

// TEST 4: BLE Advertising Latency
void testBLEAdvertisingLatency() {
  printDivider();
  Serial.println("TEST 4: BLE Advertising Start/Stop Latency");
  printDivider();

  unsigned long totalStart = 0;
  unsigned long totalStop = 0;

  for (int i = 0; i < testRuns; i++) {
    BLEDevice::init("M5Stick-Sensor");
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();

    // Set advertising data
    BLEAdvertisementData advData;
    advData.setName("M5Stick-Sensor");
    pAdvertising->setAdvertisementData(advData);

    // Measure start time
    t1 = micros();
    pAdvertising->start();
    t2 = micros();
    unsigned long startTime = t2 - t1;
    totalStart += startTime;

    delay(100);

    // Measure stop time
    t1 = micros();
    pAdvertising->stop();
    t2 = micros();
    unsigned long stopTime = t2 - t1;
    totalStop += stopTime;

    BLEDevice::deinit(true);
    delay(300);

    Serial.print("  Run "); Serial.print(i+1);
    Serial.print(": Start="); Serial.print(startTime);
    Serial.print(" us | Stop="); Serial.print(stopTime); Serial.println(" us");
  }

  Serial.print("  AVG Start: "); Serial.print(totalStart / testRuns); Serial.println(" us");
  Serial.print("  AVG Stop:  "); Serial.print(totalStop / testRuns); Serial.println(" us");
  Serial.println();
}

// TEST 5: BLE vs WiFi Startup Overhead
void testStartupOverhead() {
  printDivider();
  Serial.println("TEST 5: Protocol Startup Overhead Comparison");
  printDivider();

  // BLE init time
  unsigned long bleTotal = 0;
  for (int i = 0; i < testRuns; i++) {
    t1 = micros();
    BLEDevice::init("");
    t2 = micros();
    bleTotal += (t2 - t1);
    BLEDevice::deinit(true);
    delay(300);
  }
  Serial.print("  BLE init avg:  "); Serial.print(bleTotal / testRuns); Serial.println(" us");

  // WiFi init time
  unsigned long wifiTotal = 0;
  for (int i = 0; i < testRuns; i++) {
    WiFi.mode(WIFI_OFF);
    delay(500);
    t1 = micros();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) delay(10);
    t2 = micros();
    wifiTotal += (t2 - t1);
    WiFi.mode(WIFI_OFF);
    delay(500);
  }
  Serial.print("  WiFi connect avg: "); Serial.print(wifiTotal / testRuns); Serial.println(" us");
  Serial.println();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  delay(1000);


  Serial.println("  IoT Protocol Latency Benchmark");
  Serial.println("  M5StickC Plus");
  Serial.println();

  // Run all tests
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Test 1: WiFi");
  testWiFiConnectionTime();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Test 2: MQTT");
  testMQTTPublishLatency();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Test 3: BLE Scan");
  testBLEScanLatency();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Test 4: BLE Adv");
  testBLEAdvertisingLatency();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Test 5: Overhead");
  testStartupOverhead();

  Serial.println("  ALL TESTS COMPLETE");

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.println("Done!");
  M5.Lcd.println("Check Serial");
}

void loop() {}   