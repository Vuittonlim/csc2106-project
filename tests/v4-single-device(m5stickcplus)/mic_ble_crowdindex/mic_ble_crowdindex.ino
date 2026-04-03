#include <M5Unified.h>
#include "http_handler.h"
#include "mqtt_handler.h"

String currentSoundLevel = "Low";
String currentCrowdLabel = "Low";
bool mqttPaused = false;

// Smoothing config
#define SMOOTH_SIZE 10
int peakHistory[SMOOTH_SIZE] = {0};
int smoothIndex = 0;
int16_t buffer[256];

// Rolling average
int getSmoothedPeak(int newPeak) {
  peakHistory[smoothIndex] = newPeak;
  smoothIndex = (smoothIndex + 1) % SMOOTH_SIZE;
  int sum = 0;
  for (int i = 0; i < SMOOTH_SIZE; i++) sum += peakHistory[i];
  return sum / SMOOTH_SIZE;
}

// Crowd level from sound only
String getCrowdLabel(String soundLevel) {
  if (soundLevel == "High") return "High";
  else if (soundLevel == "Medium") return "Medium";
  else return "Low";
}

// Display
void updateDisplay(String soundLevel, int smoothedPeak, String crowdLabel, uint16_t color) {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 5);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.print("Sound: "); M5.Lcd.println(soundLevel);
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.print("Peak: "); M5.Lcd.println(smoothedPeak);
  M5.Lcd.setCursor(10, 55);
  M5.Lcd.setTextColor(color);
  M5.Lcd.print("Crowd: "); M5.Lcd.println(crowdLabel);
  M5.Lcd.setCursor(10, 83);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);
  if (crowdLabel == "Low")         M5.Lcd.println("Good time to visit!");
  else if (crowdLabel == "Medium") M5.Lcd.println("Moderate crowd");
  else                             M5.Lcd.println("Very crowded!");
  M5.Lcd.setCursor(10, 100);
  M5.Lcd.setTextColor(mqttPaused ? RED : GREEN);
  M5.Lcd.println(mqttPaused ? "MQTT: PAUSED" : "MQTT: ON");
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

  setupHTTP();

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("Starting...");

  setupMQTT();
  connectMQTT();

  delay(500);
  M5.Lcd.fillScreen(BLACK);
  Serial.println("Ready!");
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    mqttPaused = !mqttPaused;
    Serial.println(mqttPaused ? "MQTT paused (fallback test)" : "MQTT resumed");
  }

  // Step 1: Read mic
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

  // Step 2: Crowd level
  String crowdLabel = getCrowdLabel(soundLevel);
  uint16_t crowdColor;
  if (crowdLabel == "Low")         crowdColor = GREEN;
  else if (crowdLabel == "Medium") crowdColor = YELLOW;
  else                              crowdColor = RED;

  currentSoundLevel = soundLevel;
  currentCrowdLabel = crowdLabel;

  // Step 3: Display
  updateDisplay(soundLevel, smoothedPeak, crowdLabel, crowdColor);

  // Step 4: HTTP & MQTT
  handleHTTP();
  if (!mqttPaused) handleMQTT(soundLevel, 0, crowdLabel, 1);
}