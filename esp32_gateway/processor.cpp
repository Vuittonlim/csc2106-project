#include "data_types.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern QueueHandle_t dataQueue;
extern PubSubClient client;
int confidence = 1;
// extern int confidence;
extern void publishAnalytics(String payload);

// ─────────────────────────
// ZONE DATA
// ─────────────────────────
int seating1_Count = 0;
int seating2_Count = 0;
int seating3_Count = 0;

// ─────────────────────────
// FLOW DATA (CoAP)
// ─────────────────────────
int totalPeople = 0;
int lastFlow = 0;

// ─────────────────────────
// CONFIG
// ─────────────────────────
const int MAX_SEATING1 = 140;
const int MAX_SEATING2 = 320;
const int MAX_SEATING3 = 64;

const unsigned long COMPUTE_INTERVAL = 120000; // 2 mins

// ─────────────────────────
// HISTORY
// ─────────────────────────
#define HISTORY_SIZE 20
#define HISTORY_SHORT 10
#define HISTORY_LONG 20

float crowdHistory[HISTORY_SIZE];
int historyIndex = 0;
bool historyFilled = false;

// ─────────────────────────
// HIGH TRACKING
// ─────────────────────────
unsigned long highStartTime = 0;
bool isHighOngoing = false;

// ─────────────────────────
// TREND FUNCTION (FIXED)
// ─────────────────────────
float calculateTrend(int windowSize) {

  int n = historyFilled ? windowSize : historyIndex;
  if (n < 2) return 0;

  if (n > windowSize) n = windowSize;

  float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;

  for (int i = 0; i < n; i++) {

    int index = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;

    float x = i;
    float y = crowdHistory[index];

    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumXX += x * x;
  }

  float denominator = (n * sumXX - sumX * sumX);
  if (denominator == 0) return 0;

  float slope = (n * sumXY - sumX * sumY) / denominator;

  return slope;
}

// ─────────────────────────
// TREND LABEL
// ─────────────────────────
String getTrendLabel(float slope) {
  if (slope > 0.02) return "INCREASING";
  else if (slope < -0.02) return "DECREASING";
  else return "STABLE";
}

// ─────────────────────────
// MAIN TASK
// ─────────────────────────
void processingTask(void *pvParameters) {

  DataPacket packet;
  unsigned long lastCompute = 0;

  while (true) {

    // ────────────────
    // 1. RECEIVE DATA
    // ────────────────
    if (xQueueReceive(dataQueue, &packet, 100 / portTICK_PERIOD_MS)) {

      Serial.println("\n[PROCESS] Source: " + packet.source);

      if (packet.source == "coap") {

        totalPeople += packet.count;
        if (totalPeople < 0) totalPeople = 0;

        lastFlow = packet.count;

        Serial.println("[FLOW] Total People: " + String(totalPeople));
      }

      else {

        if (packet.zone == "seating_1") seating1_Count = packet.count;
        else if (packet.zone == "seating_2") seating2_Count = packet.count;
        else if (packet.zone == "seating_3") seating3_Count = packet.count;
      }
    }

    // ────────────────
    // 2. COMPUTE
    // ────────────────
    unsigned long currentMillis = millis();

    if (currentMillis - lastCompute >= COMPUTE_INTERVAL) {

      Serial.println("\n[COMPUTE] Running analytics...");

      // Density
      float d1 = (float)seating1_Count / MAX_SEATING1;
      float d2 = (float)seating2_Count / MAX_SEATING2;
      float d3 = (float)seating3_Count / MAX_SEATING3;

      float overallDensity = (d1 + d2 + d3) / 3.0;

      // Flow factor
      float flowFactor = 0;
      if (lastFlow > 0) flowFactor = 0.2;
      else if (lastFlow < 0) flowFactor = -0.2;

      // Crowd score
      float crowdScore = (0.8 * overallDensity) + (0.2 * flowFactor);

      if (crowdScore < 0) crowdScore = 0;
      if (crowdScore > 1) crowdScore = 1;

      // Save history
      crowdHistory[historyIndex] = crowdScore;

      historyIndex++;
      if (historyIndex >= HISTORY_SIZE) {
        historyIndex = 0;
        historyFilled = true;
      }

      // ────────────────
      // LEVEL FIRST (FIXED ORDER)
      // ────────────────
      String level;

      if (crowdScore < 0.3) level = "LOW";
      else if (crowdScore < 0.7) level = "MEDIUM";
      else level = "HIGH";

      // ────────────────
      // ADAPTIVE HISTORY
      // ────────────────
      int currentHistorySize;

      if (level == "HIGH") currentHistorySize = HISTORY_SHORT;
      else currentHistorySize = HISTORY_LONG;

      // ────────────────
      // TREND
      // ────────────────
      float slope = calculateTrend(currentHistorySize);
      float predicted = crowdScore + (slope * 5);
      String trend = getTrendLabel(slope);

      if (predicted < 0) predicted = 0;
      if (predicted > 1) predicted = 1;

      // ────────────────
      // MIN CROWD (RECENT WINDOW)
      // ────────────────
      float minCrowd = 1.0;

      int n = historyFilled ? currentHistorySize : historyIndex;
      if (n > currentHistorySize) n = currentHistorySize;

      for (int i = 0; i < n; i++) {

        int index = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;

        if (crowdHistory[index] < minCrowd) {
          minCrowd = crowdHistory[index];
        }
      }

      // ────────────────
      // HIGH DURATION
      // ────────────────
      if (level == "HIGH") {

        if (!isHighOngoing) {
          highStartTime = millis();
          isHighOngoing = true;
        }

      } else {
        isHighOngoing = false;
      }

      bool prolongedHigh = false;

      if (isHighOngoing) {
        if (millis() - highStartTime >= 600000) {
          prolongedHigh = true;
        }
      }

      // ────────────────
      // BEST TIME
      // ────────────────
      String bestTime;

      if (prolongedHigh) bestTime = "AVOID (PEAK)";
      else if (predicted < 0.3) bestTime = "NOW";
      else if (minCrowd < 0.3) bestTime = "SOON";
      else bestTime = "LATER";

      // DEBUG
      Serial.println("Density: " + String(overallDensity));
      Serial.println("Score: " + String(crowdScore));
      Serial.println("Predicted: " + String(predicted));
      Serial.println("Trend: " + trend);
      Serial.println("Level: " + level);
      Serial.println("Best Time: " + bestTime);

      // ────────────────
      // MQTT OUTPUT
      // ────────────────
      if (client.connected()) {

        String payload = "{";
        payload += "\"totalPeople\":" + String(totalPeople) + ",";
        payload += "\"density\":" + String(overallDensity) + ",";
        payload += "\"score\":" + String(crowdScore) + ",";
        payload += "\"predicted\":" + String(predicted) + ",";
        payload += "\"trend\":\"" + trend + "\",";
        payload += "\"level\":\"" + level + "\",";
        payload += "\"bestTime\":\"" + bestTime + "\",";
        payload += "\"prolongedHigh\":" + String(prolongedHigh) + ",";
        payload += "\"confidence\":" + String(confidence) + ",";
        payload += "\"zones\":{";
        payload += "\"Seating Zone 1\":" + String(d1) + ",";
        payload += "\"Seating Zone 2\":" + String(d2) + ",";
        payload += "\"Seating Zone 3\":" + String(d3);
        payload += "}";
        payload += "}";

        publishAnalytics(payload);

        Serial.println("[MQTT OUT] " + payload);
      }

      lastCompute = currentMillis;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}