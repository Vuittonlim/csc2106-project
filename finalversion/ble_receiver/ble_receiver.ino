// BLE Fallback Receiver
// Scans for "M5Crowd" BLE advertisements and displays the crowd data.


#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#define SCAN_DURATION 3
#define SCAN_INTERVAL 5000  // scan every 5 seconds

BLEScan* pBLEScan;
unsigned long lastScan = 0;

// Latest received data
String rxSound = "?";
String rxBle = "?";
String rxCrowd = "?";
String rxConf = "?";
bool dataReceived = false;
unsigned long lastDataTime = 0;

// Expand single-letter labels
String expandLabel(String code) {
  if (code == "L") return "Low";
  if (code == "M") return "Medium";
  if (code == "H") return "High";
  return code;
}

class BLEReceiver : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    if (device.getName() != "M5Crowd") return;

    Serial.println("Found M5Crowd!");

    if (device.haveManufacturerData()) {
      String raw = device.getManufacturerData();

      // Skip first 2 bytes (manufacturer ID)
      if (raw.length() > 2) {
        String payload = raw.substring(2);
        Serial.print("BLE payload: "); Serial.println(payload);

        // sound|bleCount|crowd|confidence
        int p1 = payload.indexOf('|');
        int p2 = payload.indexOf('|', p1 + 1);
        int p3 = payload.indexOf('|', p2 + 1);

        if (p1 > 0 && p2 > 0 && p3 > 0) {
          rxSound = expandLabel(payload.substring(0, p1));
          rxBle   = payload.substring(p1 + 1, p2);
          rxCrowd = expandLabel(payload.substring(p2 + 1, p3));
          rxConf  = payload.substring(p3 + 1);
          dataReceived = true;
          lastDataTime = millis();

          Serial.print("Sound: "); Serial.println(rxSound);
          Serial.print("BLE count: "); Serial.println(rxBle);
          Serial.print("Crowd: "); Serial.println(rxCrowd);
          Serial.print("Confidence: "); Serial.println(rxConf);
        }
      }
    }
  }
};

void updateDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  // Title
  M5.Lcd.setCursor(10, 5);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.println("BLE Receiver");

  if (!dataReceived) {
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("Scanning for");
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.println("M5Crowd...");
    return;
  }

  // Check if data is stale (>30s old)
  bool stale = (millis() - lastDataTime) > 30000;

  M5.Lcd.setCursor(10, 30);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.print("Sound: "); M5.Lcd.println(rxSound);

  M5.Lcd.setCursor(10, 55);
  M5.Lcd.print("BLE: "); M5.Lcd.println(rxBle);

  M5.Lcd.setCursor(10, 80);
  uint16_t crowdColor = GREEN;
  if (rxCrowd == "Medium") crowdColor = YELLOW;
  else if (rxCrowd == "High") crowdColor = RED;
  M5.Lcd.setTextColor(crowdColor);
  M5.Lcd.print("Crowd: "); M5.Lcd.println(rxCrowd);

  M5.Lcd.setCursor(10, 108);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.print("Confidence: "); M5.Lcd.println(rxConf);

  M5.Lcd.setCursor(10, 125);
  if (stale) {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.println("DATA STALE - no signal");
  } else {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.println("LIVE via BLE");
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  Serial.begin(115200);

  BLEDevice::init("M5Receiver");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->setAdvertisedDeviceCallbacks(new BLEReceiver());

  Serial.println("BLE Receiver ready — scanning for M5Crowd...");
  updateDisplay();
}

void loop() {
  M5.update();

  unsigned long now = millis();
  if (now - lastScan >= SCAN_INTERVAL || lastScan == 0) {
    lastScan = now;
    Serial.println("Scanning...");
    pBLEScan->start(SCAN_DURATION, false);
    pBLEScan->clearResults();
  }

  updateDisplay();
  delay(500);
}
