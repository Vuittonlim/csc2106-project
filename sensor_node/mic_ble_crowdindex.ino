#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>

#define SCAN_DURATION 2
#define BLE_SCAN_INTERVAL 30000
unsigned long lastBLEScan = 0;

//  Smoothing config 
#define SMOOTH_SIZE 10  // average over 10 readings
int peakHistory[SMOOTH_SIZE] = {0};
int smoothIndex = 0;

int16_t buffer[256];
int bleCount = 0;
BLEScan* pBLEScan;
BLEAdvertising* pAdvertising;
bool bleInitialised = false;

//  Rolling average for peak 
int getSmoothedPeak(int newPeak) {
  peakHistory[smoothIndex] = newPeak;
  smoothIndex = (smoothIndex + 1) % SMOOTH_SIZE;
  int sum = 0;
  for (int i = 0; i < SMOOTH_SIZE; i++) sum += peakHistory[i];
  return sum / SMOOTH_SIZE;
}

//  Crowd index 
int getCrowdIndex(String soundLevel, int bleCount) {
  int soundScore = 0;
  if (soundLevel == "Medium") soundScore = 1;
  else if (soundLevel == "High") soundScore = 2;

  int bleScore = 0;
  if (bleCount >= 5 && bleCount < 15) bleScore = 1;
  else if (bleCount >= 15) bleScore = 2;

  int combined = soundScore + bleScore;
  if (combined <= 1) return 0;
  else if (combined <= 3) return 1;
  else return 2;
}

//  Display 
void updateDisplay(String soundLevel, int smoothedPeak, int ble, String crowdLabel, uint16_t color) {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  M5.Lcd.setCursor(10, 5);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.print("Sound: "); M5.Lcd.println(soundLevel);

  M5.Lcd.setCursor(10, 30);
  M5.Lcd.print("Peak: "); M5.Lcd.println(smoothedPeak);

  M5.Lcd.setCursor(10, 55);
  M5.Lcd.print("BLE: "); M5.Lcd.println(ble);

  M5.Lcd.setCursor(10, 80);
  M5.Lcd.setTextColor(color);
  M5.Lcd.print("Crowd: "); M5.Lcd.println(crowdLabel);

  M5.Lcd.setCursor(10, 108);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);
  if (crowdLabel == "Low")         M5.Lcd.println("Good time to visit!");
  else if (crowdLabel == "Medium") M5.Lcd.println("Moderate crowd");
  else                             M5.Lcd.println("Very crowded!");
  M5.Lcd.setTextSize(2);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Mic.begin();
  Serial.begin(115200);

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("Starting...");

  // Init BLE scan once
  BLEDevice::init("M5Crowd");
  bleInitialised = true;
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  delay(500);
  M5.Lcd.fillScreen(BLACK);
  Serial.println("Ready!");
}

void loop() {
  // Step 1: Read mic + smooth 
  int rawPeak = 0;
  if (M5.Mic.record(buffer, 256, 16000)) {
    for (int i = 0; i < 256; i++) {
      if (abs(buffer[i]) > rawPeak) rawPeak = abs(buffer[i]);
    }
  }

  int smoothedPeak = getSmoothedPeak(rawPeak);

  //  Thresholds 
  // LOW:    0     - 500   (silent room)
  // MEDIUM: 500   - 2000  (talking nearby)
  // HIGH:   2000+         (loud talking / multiple people)
  // NOTE: increase these thresholds when testing in actual canteen
  String soundLevel;
  if (smoothedPeak > 2000)     soundLevel = "High";
  else if (smoothedPeak > 500) soundLevel = "Medium";
  else                          soundLevel = "Low";

  Serial.print("Raw: "); Serial.print(rawPeak);
  Serial.print(" | Smoothed: "); Serial.print(smoothedPeak);
  Serial.print(" | Level: "); Serial.println(soundLevel);

  //  Step 2: BLE Scan every 30 seconds 
  unsigned long now = millis();
  if (now - lastBLEScan >= BLE_SCAN_INTERVAL || lastBLEScan == 0) {
    lastBLEScan = now;
    bleCount = 0;
    Serial.println("Running BLE scan...");
    BLEScanResults* results = pBLEScan->start(SCAN_DURATION, false);
    bleCount = results->getCount();
    pBLEScan->clearResults();
    Serial.print("BLE devices: "); Serial.println(bleCount);
  }

  //  Step 3: Crowd index 
  int crowdIndex = getCrowdIndex(soundLevel, bleCount);
  String crowdLabel;
  uint16_t crowdColor;
  if (crowdIndex == 0)      { crowdLabel = "Low";    crowdColor = GREEN; }
  else if (crowdIndex == 1) { crowdLabel = "Medium"; crowdColor = YELLOW; }
  else                      { crowdLabel = "High";   crowdColor = RED; }

  //  Step 4: Update display 
  updateDisplay(soundLevel, smoothedPeak, bleCount, crowdLabel, crowdColor);

   String payload = "{\"s\":\"" + soundLevel.substring(0,1) +
                   "\",\"b\":" + String(bleCount) +
                   ",\"c\":\"" + crowdLabel.substring(0,1) + "\"}";
  Serial.print("Broadcasting: "); Serial.println(payload);

 //  Step 5: Advertise crowd data 
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setName("M5Crowd");
  String manfData = "";
  manfData += (char)0xFF;
  manfData += (char)0xFF;
  manfData += payload;
  advData.setManufacturerData(manfData);
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();

  delay(1000); // advertise for 1 second

  pAdvertising->stop();
  delay(100);
  // No deinit - BLE stays alive for next scan
}