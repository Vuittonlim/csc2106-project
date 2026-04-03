#include <M5Unified.h>
#include "ble_handler.h"

// Global state 
String currentSoundLevel = "Low";
String currentCrowdLabel = "Low";
int bleCount = 0;
int crowdConfidence = 1;

// Mic smoothing (RMS)
#define SMOOTH_SIZE 10
int rmsHistory[SMOOTH_SIZE] = {0};
int smoothIndex = 0;
int16_t buffer[256];

int getSmoothedRMS(int newRMS) {
  rmsHistory[smoothIndex] = newRMS;
  smoothIndex = (smoothIndex + 1) % SMOOTH_SIZE;
  int sum = 0;
  for (int i = 0; i < SMOOTH_SIZE; i++) sum += rmsHistory[i];
  return sum / SMOOTH_SIZE;
}

//Crowd index logic
int getCrowdIndex(String soundLevel, int bleCount) {
  int soundScore = 0;
  if (soundLevel == "Medium") soundScore = 1;
  else if (soundLevel == "High") soundScore = 2;

  int bleScore = 0;
  if (bleCount >= 15 && bleCount < 25) bleScore = 1;  // Medium
  else if (bleCount >= 25) bleScore = 2;                // High

  int diff = abs(soundScore - bleScore);
  if (diff >= 2) {
    crowdConfidence = 0;
    Serial.println("WARNING: sensors disagree, low confidence");
    return bleScore;
  } else {
    crowdConfidence = 1;
  }

  int combined = soundScore + bleScore;
  if (combined <= 1) return 0;
  else if (combined <= 3) return 1;
  else return 2;
}

// Display 
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

// UART to Pico W
void sendToPicoW(String soundLevel, int bleCount, String crowdLabel, int confidence) {
  String payload = "{\"s\":\"" + soundLevel.substring(0, 1) + "\""
                   ",\"b\":" + String(bleCount) +
                   ",\"c\":\"" + crowdLabel.substring(0, 1) + "\""
                   ",\"q\":" + String(confidence) + "}";
  Serial2.println(payload);
  Serial.println("UART sent: " + payload);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Mic.begin();
  Serial.begin(115200);

  // UART to Pico W: TX=G26, RX=G36
  Serial2.begin(9600, SERIAL_8N1, 36, 26);

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("Starting...");

  setupBLE();

  delay(500);
  M5.Lcd.fillScreen(BLACK);
  Serial.println("Ready!");
}

void loop() {
  M5.update();

  // Step 1: Read mic (RMS)
  int rms = 0;
  if (M5.Mic.record(buffer, 256, 16000)) {
    int64_t sumSquares = 0;
    for (int i = 0; i < 256; i++) {
      sumSquares += (int64_t)buffer[i] * buffer[i];
    }
    rms = (int)sqrt((double)sumSquares / 256);
  }

  int smoothedRMS = getSmoothedRMS(rms);

  String soundLevel;
  if (smoothedRMS > 1400)      soundLevel = "High";
  else if (smoothedRMS > 800) soundLevel = "Medium";
  else                          soundLevel = "Low";

  // Step 2: BLE Scan
  scanBLE();

  // Step 3: Crowd index
  int crowdIndex = getCrowdIndex(soundLevel, bleCount);
  String crowdLabel;
  uint16_t crowdColor;
  if (crowdIndex == 0)      { crowdLabel = "Low";    crowdColor = GREEN; }
  else if (crowdIndex == 1) { crowdLabel = "Medium"; crowdColor = YELLOW; }
  else                      { crowdLabel = "High";   crowdColor = RED; }

  currentSoundLevel = soundLevel;
  currentCrowdLabel = crowdLabel;

  // Step 4: Display
  updateDisplay(soundLevel, smoothedRMS, bleCount, crowdLabel, crowdColor);

  // Step 5: Send over UART to Pico W
  sendToPicoW(soundLevel, bleCount, crowdLabel, crowdConfidence);

  // Step 6: BLE Advertise
  advertiseBLE(soundLevel, bleCount, crowdLabel, crowdConfidence);
}
