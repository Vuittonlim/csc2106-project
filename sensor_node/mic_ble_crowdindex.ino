#include <M5Unified.h>
#include "ble_handler.h"
#include "http_handler.h"
#include "mqtt_handler.h"


String currentSoundLevel = "Low";
String currentCrowdLabel = "Low";
//  Smoothing config
#define SMOOTH_SIZE 10
int peakHistory[SMOOTH_SIZE] = {0};
int smoothIndex = 0;
int16_t buffer[256];
int bleCount = 0;
int crowdConfidence = 1; // 1 = confident, 0 = uncertain

//  Rolling average
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

  // Confidence check
  int diff = abs(soundScore - bleScore);
  if (diff >= 2) {
    crowdConfidence = 0;  // sensors strongly disagree
    Serial.println("WARNING: sensors disagree, low confidence");
    return bleScore;      // trust BLE over sound when disagreement is high
  } else {
    crowdConfidence = 1;
  }

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

  // setup()
  setupBLE();
  setupHTTP();
  setupMQTT();

  delay(500);
  M5.Lcd.fillScreen(BLACK);
  Serial.println("Ready!");
}

void loop() {
  //  Step 1: Read mic
  int rawPeak = 0;
  if (M5.Mic.record(buffer, 256, 16000)) {
    for (int i = 0; i < 256; i++) {
      if (abs(buffer[i]) > rawPeak) rawPeak = abs(buffer[i]);
    }
  }

  int smoothedPeak = getSmoothedPeak(rawPeak);

  String soundLevel;
  if (smoothedPeak > 2000)     soundLevel = "High";
  else if (smoothedPeak > 500) soundLevel = "Medium";
  else                          soundLevel = "Low";

  Serial.print("Raw: "); Serial.print(rawPeak);
  Serial.print(" | Smoothed: "); Serial.print(smoothedPeak);
  Serial.print(" | Level: "); Serial.println(soundLevel);

  //  Step 2: BLE Scan
  scanBLE();

  //  Step 3: Crowd index
  int crowdIndex = getCrowdIndex(soundLevel, bleCount);
  String crowdLabel;
  uint16_t crowdColor;
  if (crowdIndex == 0)      { crowdLabel = "Low";    crowdColor = GREEN; }
  else if (crowdIndex == 1) { crowdLabel = "Medium"; crowdColor = YELLOW; }
  else                      { crowdLabel = "High";   crowdColor = RED; }

  currentSoundLevel = soundLevel;
  currentCrowdLabel = crowdLabel;

  //  Step 4: Display
  updateDisplay(soundLevel, smoothedPeak, bleCount, crowdLabel, crowdColor);

  handleHTTP();
  handleMQTT(soundLevel, bleCount, crowdLabel, crowdConfidence);

  //  Step 5: BLE Advertise
  advertiseBLE(soundLevel, bleCount, crowdLabel, crowdConfidence);
}
