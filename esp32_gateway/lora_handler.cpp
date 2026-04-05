/**
 * @file lora_handler.cpp
 * @brief LoRa UART receiver for zone occupancy, PIR, and environmental sensor data.
 *
 * Reads newline-terminated frames from a LoRa radio module connected via UART
 * (Serial2). Each frame carries a prefixed RSSI value and a compact JSON body.
 * The JSON is parsed, the crowd-level code is converted to a human-readable
 * label, and the result is forwarded onto the shared dataQueue as a DataPacket.
 *
 * Expected frame format:
 *   "RSSI: -61 | 183|{"s":"M","zone":"seating_1","pir":1,"t":27.5,"h":60.2}"
 *    └─ prefix ─┘ └snr┘└─────────────── JSON body ──────────────────────────┘
 */
#include "lora_handler.h"
#include <ArduinoJson.h>

/**
 * @brief Initialise Serial2 for UART communication with the LoRa module.
 *
 * Uses 115200 baud, 8N1 framing on the RXD2/TXD2 pins defined in config.h.
 * Must be called once in setup() before loraTask() is started.
 */
void initLoRaUART() {
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
    Serial.println("[LoRa] UART initialized");
}

/**
 * @brief Extract the numeric zone identifier from a full zone name string.
 *
 * Examples:
 *   "seating_1" → "1"
 *   "seating_2" → "2"
 *   "2"         → "2"  (already numeric — returned unchanged)
 *
 * @param zoneFull Full zone name as transmitted in the LoRa JSON payload.
 * @return Numeric zone string, or the original string if no '_' is found.
 */
static String zoneNameToNumber(const String& zoneFull) {
    int idx = zoneFull.lastIndexOf('_');
    if (idx >= 0) return zoneFull.substring(idx + 1);
    return zoneFull;
}

/**
 * @brief FreeRTOS task — read LoRa UART frames and enqueue DataPackets.
 *
 * Polls Serial2 every 10 ms. When a complete newline-terminated frame
 * arrives, the task:
 *  1. Splits on '|' separators to isolate the RSSI prefix and JSON body.
 *  2. Parses the JSON — recognised fields:
 *       - "s"    (str)   : crowd level code "L", "M", or "H".
 *       - "zone" (str)   : full zone name e.g. "seating_1".
 *       - "pir"  (int)   : PIR motion value (logged only, not queued).
 *       - "t"    (float) : ambient temperature in °C.
 *       - "h"    (float) : relative humidity in % (logged only, not queued).
 *  3. Converts the level code to a label via crowdLevelToLabel().
 *  4. Builds a DataPacket with:
 *       - packet.payload = level label ("Low" / "Medium" / "High")
 *       - packet.count   = 0  (LoRa does not provide a raw count)
 *       - packet.temp    = temperature in °C
 *  5. Pushes the packet onto dataQueue for processingTask().
 *
 * @param pvParameters Unused (required by FreeRTOS task signature).
 */
/**
 * @brief Map a compact crowd level code to a human-readable label.
 *
 * The Pico W transmits single-character codes in the "c" field:
 *   "L" → "Low", "M" → "Medium", "H" → "High"
 *
 * @param level Single-character level code from the LoRa JSON payload.
 * @return Pointer to a static label string.
 */
static const char* crowdLevelToLabel(const String& level) {
    if (level == "H") return "High";
    if (level == "M") return "Medium";
    return "Low";
}

void loraTask(void *pvParameters) {
    while (true) {
        if (Serial2.available()) {
            String raw = Serial2.readStringUntil('\n');
            raw.trim();
            Serial.println("[LoRa RX] " + raw);

            int firstPipe  = raw.indexOf('|');
            int secondPipe = raw.indexOf('|', firstPipe + 1);

            if (firstPipe < 0 || secondPipe < 0) {
                Serial.println("[LoRa] Unexpected format — skipping");
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }

            String rssiStr = raw.substring(raw.indexOf(':') + 1, firstPipe);
            int rssi = rssiStr.toInt();

            String jsonPart = raw.substring(secondPipe + 1);
            jsonPart.trim();

            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, jsonPart);

            if (err) {
                Serial.println("[LoRa] JSON parse failed: " + String(err.c_str()));
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }

            // Extract fields — all default to safe values if the key is absent
            // LoRa payload uses "s" for raw acoustic noise classification.
            // "c" (fused) is NOT present in LoRa frames — only in MQTT fallback.
            // The processor will fuse s + pir + humidity into d1.
            String soundLevel = doc["s"]    | "L";   // raw acoustic: "L"/"M"/"H"
            String zoneFull   = doc["zone"] | "seating_1";
            int    pir        = doc["pir"]  | 0;
            float  temp       = doc["t"]    | 0.0;
            float  humidity   = doc["h"]    | 0.0;

            String zoneNum           = zoneNameToNumber(zoneFull);
            const char* levelLabel   = crowdLevelToLabel(soundLevel);

            Serial.print("[LoRa] RSSI="); Serial.print(rssi);
            Serial.print(" | zone=");    Serial.print(zoneNum);
            Serial.print(" | sound=");   Serial.print(levelLabel);
            Serial.print(" | pir=");     Serial.print(pir);
            Serial.print(" | t=");       Serial.print(temp, 1);
            Serial.print(" | h=");       Serial.println(humidity, 1);

            // packet.payload = acoustic level label ("Low"/"Medium"/"High") from "s"
            // packet.pir     = raw PIR value — processor uses this to boost/reduce d1
            // packet.humidity = for humidity-based density nudge in processor
            DataPacket packet;
            packet.confidence = 1;
            packet.pir        = pir;
            packet.humidity   = humidity;
            packet.sound[0]   = '\0';
            strncpy(packet.source, "lora", sizeof(packet.source) - 1);
            packet.source[sizeof(packet.source) - 1] = '\0';
            strncpy(packet.zone, zoneNum.c_str(), sizeof(packet.zone) - 1);
            packet.zone[sizeof(packet.zone) - 1] = '\0';
            packet.count = 0;
            packet.temp  = temp;
            strncpy(packet.payload, levelLabel, sizeof(packet.payload) - 1);
            packet.payload[sizeof(packet.payload) - 1] = '\0';

            if (xQueueSend(dataQueue, &packet, pdMS_TO_TICKS(200)) == pdPASS) {
                Serial.printf("[LoRa -> QUEUE] zone=%s level=%s\n", packet.zone, packet.payload);
            } else {
                Serial.println("[LoRa] Queue FULL — packet dropped");  // Yield 10 ms before polling Serial2 again
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
