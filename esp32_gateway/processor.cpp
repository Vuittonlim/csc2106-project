/**
 * @file processing.cpp
 * @brief Crowd analytics processing task for multi-zone occupancy monitoring.
 *
 * Receives sensor data packets from a FreeRTOS queue, aggregates zone-level
 * occupancy and temperature readings, computes a rolling crowd score, detects
 * trends, and publishes analytics via MQTT.
 */

#include "data_types.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/** --- External dependencies ---*/
extern QueueHandle_t dataQueue;  ///< Shared queue feeding incoming sensor packets
extern PubSubClient client;      ///< MQTT client for publishing analytics
int confidence = 1;              ///< Confidence level for published analytics (1 = default)
extern void publishAnalytics(String payload);

// --- Zone 1 state ---
float seating1_Temp     = 0.0f;
float seating1_Humidity = 0.0f;
int   seating1_PIR      = 0;
char  seating1_Level[16] = "Low"; ///< Acoustic label: "s" (LoRa) or "c" (MQTT fallback)

// --- Zone 2 state ---
int   seating2_Count      = 0;    ///< Raw BLE scan count
int   seating2_Confidence = 1;    ///< 1 = sound+BLE agree; 0 = disagree
float seating2_Temp       = 0.0f;
char  seating2_Level[16]  = "Low";
char  seating2_Crowd[16]  = "Low"; ///< Pre-fused crowd label (confidence=1 path)
char  seating2_Sound[16]  = "Low"; ///< Raw acoustic label (confidence=0 blend path)

// --- Entry/exit flow state ---
int   totalPeople   = 0;     ///< Running total of people (from CoAP)
int   lastFlow      = 0;     ///< Direction of last flow event: +1 = entry, -1 = exit
float entrance_Temp = 0.0f;  ///< Entrance temperature from CoAP packet

// --- Zone capacity limits ---
const int MAX_SEATING1 = 140;
const int MAX_SEATING2 = 320;

/// Interval (ms) between analytics computation cycles
const unsigned long COMPUTE_INTERVAL = 15000;

// --- Trend history ring buffer ---
#define HISTORY_SIZE  20
#define HISTORY_SHORT 10
#define HISTORY_LONG  20

float crowdHistory[HISTORY_SIZE];
int   historyIndex  = 0;
bool  historyFilled = false;

// --- Prolonged-high tracking ---
unsigned long highStartTime = 0;
bool          isHighOngoing = false;

// ─────────────────────────────────────────
// Helper: map level string to a float (0–1)
// ─────────────────────────────────────────
static float levelToFloat(const char* level) {
  if (strcmp(level, "High")   == 0) return 0.85f;
  if (strcmp(level, "Medium") == 0) return 0.55f;
  return 0.2f;   // "Low" or unknown
}

// ─────────────────────────────────────────
// Helper: rank a level (Low=0, Medium=1, High=2)
// ─────────────────────────────────────────
static int levelRank(const char* level) {
  if (strcmp(level, "High")   == 0) return 2;
  if (strcmp(level, "Medium") == 0) return 1;
  return 0;
}

// ─────────────────────────────────────────
// Helper: density (0–1) → level string
// ─────────────────────────────────────────
static const char* densityToLevel(float density) {
  if (density >= 0.7f) return "High";
  if (density >= 0.3f) return "Medium";
  return "Low";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: overall level — same → use it; different → take the higher-ranked one
// ─────────────────────────────────────────────────────────────────────────────
static const char* overallLevel(const char* l1, const char* l2) {
  if (strcmp(l1, l2) == 0) return l1;
  return (levelRank(l1) >= levelRank(l2)) ? l1 : l2;
}

/**
 * @brief Compute the linear regression slope over the most recent crowd scores.
 */
float calculateTrend(int windowSize) {
  int n = historyFilled ? windowSize : historyIndex;
  if (n < 2) return 0;
  if (n > windowSize) n = windowSize;
  float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
  for (int i = 0; i < n; i++) {
    int   idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    float x = i, y = crowdHistory[idx];
    sumX += x; sumY += y; sumXY += x * y; sumXX += x * x;
  }
  float denom = (n * sumXX - sumX * sumX);
  if (denom == 0) return 0;
  return (n * sumXY - sumX * sumY) / denom;
}

/**
 * @brief Convert a regression slope to a trend label.
 */
const char* getTrendLabel(float slope) {
  if (slope >  0.02f) return "INCREASING";
  if (slope < -0.02f) return "DECREASING";
  return "STABLE";
}

/**
 * @brief FreeRTOS task: consume sensor packets, compute crowd analytics, and publish.
 */
void processingTask(void *pvParameters) {
  DataPacket packet;
  unsigned long lastCompute = 0;

  while (true) {
    // ── Step 1: drain the queue ──────────────────────────────────────────────
    if (xQueueReceive(dataQueue, &packet, 100 / portTICK_PERIOD_MS)) {
      Serial.print("\n[PROCESS] Source: ");
      Serial.print(packet.source);
      Serial.print(" | Zone: ");
      Serial.print(packet.zone);
      Serial.print(" | Count: ");
      Serial.println(packet.count);

      if (strcmp(packet.source, "coap") == 0) {
        // CoAP carries an absolute people count, ENTER/EXIT event, and temperature
        totalPeople = packet.count;
        lastFlow    = (strcmp(packet.payload, "EXIT") == 0) ? -1 : 1;
        if (packet.temp > 0) entrance_Temp = packet.temp;
        Serial.print("[FLOW] event=");   Serial.print(packet.payload);
        Serial.print(" people=");        Serial.print(totalPeople);
        Serial.print(" t=");             Serial.println(entrance_Temp, 1);

      } else if (strcmp(packet.zone, "1") == 0) {
        // Zone 1 — works for both sources:
        //   source="lora" : payload = acoustic label from "s" (raw sound), pir + humidity populated
        //   source="mqtt" : payload = fused label from "c" (PIR+sound fused on Pico W), pir + humidity populated
        if (packet.temp     > 0)    seating1_Temp     = packet.temp;
        if (packet.humidity > 0)    seating1_Humidity = packet.humidity;
        seating1_PIR = packet.pir;
        if (packet.payload[0] != '\0') {
          strncpy(seating1_Level, packet.payload, sizeof(seating1_Level) - 1);
          seating1_Level[sizeof(seating1_Level) - 1] = '\0';
        }
        Serial.print("[ZONE 1] src=");     Serial.print(packet.source);
        Serial.print(" level=");           Serial.print(seating1_Level);
        Serial.print(" pir=");             Serial.print(seating1_PIR);
        Serial.print(" humidity=");        Serial.print(seating1_Humidity, 1);
        Serial.print(" temp=");            Serial.println(seating1_Temp, 1);

      } else if (strcmp(packet.zone, "2") == 0) {
        // Zone 2 — primary MQTT; BLE fallback handled by ble_handler when MQTT silent >30s
        // packet.payload  = pre-fused crowd label ("Low"/"Medium"/"High")
        // packet.count    = raw BLE scan count (used only when confidence == 0)
        // packet.confidence = 1 (sound+BLE agree) or 0 (disagree)
        seating2_Count      = packet.count;
        seating2_Confidence = packet.confidence;
        if (packet.temp > 0) seating2_Temp = packet.temp;
        if (packet.payload[0] != '\0') {
          strncpy(seating2_Crowd, packet.payload, sizeof(seating2_Crowd) - 1);
          seating2_Crowd[sizeof(seating2_Crowd) - 1] = '\0';
        }
        if (packet.sound[0] != '\0') {
          strncpy(seating2_Sound, packet.sound, sizeof(seating2_Sound) - 1);
          seating2_Sound[sizeof(seating2_Sound) - 1] = '\0';
        }
        Serial.print("[ZONE 2] src=");    Serial.print(packet.source);
        Serial.print(" crowd=");          Serial.print(seating2_Crowd);
        Serial.print(" sound=");          Serial.print(seating2_Sound);
        Serial.print(" count=");          Serial.print(seating2_Count);
        Serial.print(" conf=");           Serial.println(seating2_Confidence);

      } else {
        Serial.print("[PROCESS] Unknown zone: ");
        Serial.println(packet.zone);
      }
    }

    // ── Step 2: periodic analytics ───────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastCompute >= COMPUTE_INTERVAL) {
      Serial.println("\n[COMPUTE] Running analytics...");

      // d1 — Zone 1: acoustic base (s or c) + PIR modifier + humidity nudge
      //
      //  LoRa path : payload = raw acoustic "s" label
      //              PIR confirms or contradicts the sound reading
      //  MQTT path : payload = Pico W fused "c" label (already accounts for PIR)
      //              PIR still applied as a secondary confirmation signal
      //
      //  Formula:
      //    base   = levelToFloat(acoustic label)   → 0.2 / 0.55 / 0.85
      //    + pir  = +0.10 if motion detected, -0.10 if no motion
      //    + hum  = +0.05 if humidity > 70 % (body heat/moisture indicator)
      //    clamped to [0.0, 1.0]
      // d1 — Zone 1
      //   base  = acoustic label (s from LoRa, c from MQTT fallback) → float
      //   + pir = ±0.10 (motion confirms or contradicts sound)
      //   + hum = +0.05 if humidity > 70% (body heat/moisture indicator)
      //   seating1_Level is then synced to the adjusted value so display
      //   and overallLevel() comparison both reflect the real computed level
      float d1_base  = levelToFloat(seating1_Level);
      float d1_pir   = seating1_PIR ? +0.10f : -0.10f;
      float d1_humid = (seating1_Humidity > 70.0f) ? +0.05f : 0.0f;
      float d1       = d1_base + d1_pir + d1_humid;
      if (d1 < 0.0f) d1 = 0.0f;
      if (d1 > 1.0f) d1 = 1.0f;
      // Sync Zone 1 display label to adjusted d1
      strncpy(seating1_Level, densityToLevel(d1), sizeof(seating1_Level) - 1);
      seating1_Level[sizeof(seating1_Level) - 1] = '\0';

      // d2 — Zone 2
      //   confidence == 1: sound and BLE agree → trust pre-fused crowd label
      //   confidence == 0: sound and BLE disagree → blend both signals equally
      //     soundScore  = levelToFloat(seating2_Sound)   → acoustic estimate
      //     bleScore    = seating2_Count / 32.0           → BLE device density
      //     d2          = (soundScore + bleScore) / 2     → average of both
      float d2;
      if (seating2_Confidence == 1) {
        d2 = levelToFloat(seating2_Crowd);
      } else {
        float soundScore = levelToFloat(seating2_Sound);
        float bleScore   = (float)seating2_Count / 32.0f;
        if (bleScore > 1.0f) bleScore = 1.0f;
        d2 = (soundScore + bleScore) / 2.0f;
      }
      strncpy(seating2_Level, densityToLevel(d2), sizeof(seating2_Level) - 1);
      seating2_Level[sizeof(seating2_Level) - 1] = '\0';

      float overallDensity = (d1 + d2) / 2.0f;

      // Average temperature — Zone 1 (LoRa/MQTT) + Entrance (CoAP)
      // Zone 2 MQTT has no temperature sensor so it is excluded
      float avgTemp = 0.0f;
      int   tempCnt = 0;
      if (seating1_Temp  > 0) { avgTemp += seating1_Temp;  tempCnt++; }
      if (entrance_Temp  > 0) { avgTemp += entrance_Temp;  tempCnt++; }
      if (tempCnt > 0) avgTemp /= tempCnt;

      // Flow adjustment
      float flowFactor = 0.0f;
      if (lastFlow > 0) flowFactor =  0.2f;
      else if (lastFlow < 0) flowFactor = -0.2f;

      // Weighted crowd score: 80% density + 20% flow, clamped [0, 1]
      float crowdScore = (0.8f * overallDensity) + (0.2f * flowFactor);
      if (crowdScore < 0.0f) crowdScore = 0.0f;
      if (crowdScore > 1.0f) crowdScore = 1.0f;

      // Store in ring buffer
      crowdHistory[historyIndex++] = crowdScore;
      if (historyIndex >= HISTORY_SIZE) { historyIndex = 0; historyFilled = true; }

      // Crowd level from score
      const char* scoreLevel;
      if      (crowdScore < 0.3f) scoreLevel = "LOW";
      else if (crowdScore < 0.7f) scoreLevel = "MEDIUM";
      else                        scoreLevel = "HIGH";

      int currentHistorySize = (strcmp(scoreLevel, "HIGH") == 0) ? HISTORY_SHORT : HISTORY_LONG;

      // Trend
      float slope     = calculateTrend(currentHistorySize);
      float predicted = crowdScore + (slope * 5);
      if (predicted < 0.0f) predicted = 0.0f;
      if (predicted > 1.0f) predicted = 1.0f;
      const char* trend = getTrendLabel(slope);

      // Min crowd in window
      float minCrowd = 1.0f;
      int n = historyFilled ? currentHistorySize : historyIndex;
      if (n > currentHistorySize) n = currentHistorySize;
      for (int i = 0; i < n; i++) {
        int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
        if (crowdHistory[idx] < minCrowd) minCrowd = crowdHistory[idx];
      }

      // Prolonged HIGH
      if (strcmp(scoreLevel, "HIGH") == 0) {
        if (!isHighOngoing) { highStartTime = millis(); isHighOngoing = true; }
      } else {
        isHighOngoing = false;
      }
      bool prolongedHigh = isHighOngoing && (millis() - highStartTime >= 600000UL);

      // Best-time recommendation
      const char* bestTime;
      if (prolongedHigh)        bestTime = "AVOID (PEAK)";
      else if (predicted < 0.3f) bestTime = "NOW";
      else if (minCrowd  < 0.3f) bestTime = "SOON";
      else                       bestTime = "LATER";

      // Overall zone level comparison
      const char* overall = overallLevel(seating1_Level, seating2_Level);
      bool zonesMatch = (strcmp(seating1_Level, seating2_Level) == 0);

      // ── Serial output ──────────────────────────────────────────────────────
      Serial.println("-----------------------------");
      Serial.print("  Zone 1 : ");
      Serial.print(seating1_Level);
      Serial.print(" | PIR: ");
      Serial.print(seating1_PIR);
      Serial.print(" | Temp: ");
      Serial.print(seating1_Temp, 1);
      Serial.print("C | Humidity: ");
      Serial.print(seating1_Humidity, 1);
      Serial.println("%");

      Serial.print("  Zone 2 : ");
      Serial.print(seating2_Level);
      Serial.print(" | Temp: ");
      Serial.print(seating2_Temp, 1);
      Serial.println("C");

      if (zonesMatch) {
        Serial.print("  Overall : Both zones match -> ");
        Serial.println(overall);
      } else {
        Serial.print("  Overall : Zones differ -> ");
        Serial.print(overall);
        Serial.println(" (higher taken)");
      }

      Serial.print("  Avg Temp  : "); Serial.print(avgTemp, 1); Serial.println("C");
      Serial.print("  Score     : ");
      Serial.println(crowdScore, 2);
      Serial.print("  Predicted : ");
      Serial.println(predicted, 2);
      Serial.print("  Trend     : ");
      Serial.println(trend);
      Serial.print("  Best Time : ");
      Serial.println(bestTime);
      Serial.println("-----------------------------");

      // ── MQTT publish ───────────────────────────────────────────────────────
      if (client.connected()) {
        StaticJsonDocument<512> doc;
        doc["totalPeople"]   = totalPeople;
        doc["density"]       = serialized(String(overallDensity, 2));
        doc["score"]         = serialized(String(crowdScore, 2));
        doc["predicted"]     = serialized(String(predicted, 2));
        doc["trend"]         = trend;
        doc["level"]         = overall;
        doc["bestTime"]      = bestTime;
        doc["prolongedHigh"] = prolongedHigh;
        doc["avgTemp"]       = serialized(String(avgTemp, 1));

        JsonObject zones = doc.createNestedObject("zones");
        zones["1"] = seating1_Level;
        zones["2"] = seating2_Level;

        String outPayload;
        serializeJson(doc, outPayload);
        publishAnalytics(outPayload);
      }

      lastCompute = now;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}
