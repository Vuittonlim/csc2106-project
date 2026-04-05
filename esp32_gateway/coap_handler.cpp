  /**
 * @file coap_handler.cpp
 * @brief CoAP server for receiving entrance entry/exit flow events.
 *
 * Listens on UDP port 5683 (the standard CoAP port) for confirmable messages
 * carrying a JSON body with people count and event type. Each valid message is
 * parsed and forwarded to the shared dataQueue as a DataPacket, then
 * acknowledged with a minimal CoAP ACK frame so the sender knows the message
 * was received.
 *
 * This handler covers the building entrance only — zone occupancy data
 * arrives separately via MQTT (wifi_handler.cpp) or BLE (ble_handler.cpp).
 */
  #include "coap_handler.h"
  #include "data_types.h"
  #include <WiFiUdp.h>
  #include <ArduinoJson.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/queue.h"
  #include "freertos/task.h"

  extern QueueHandle_t dataQueue;

  WiFiUDP udp;                    ///< UDP socket used for all CoAP communication
  const int COAP_PORT = 5683;     ///< Standard CoAP port (RFC 7252)

/**
 * @brief Start the UDP socket and begin listening for CoAP messages.
 *
 * Must be called once at startup after Wi-Fi is connected, and before
 * coapTask() is started.
 */
  void setupCoAP() {
    udp.begin(COAP_PORT);
    Serial.print("Listening on port: ");
    Serial.println(COAP_PORT);
    Serial.println("CoAP server started");
  }

/**
 * @brief FreeRTOS task — poll for incoming CoAP packets and enqueue flow events.
 *
 * Runs an infinite loop that checks for UDP datagrams every 10 ms. When a
 * packet arrives, the task:
 *  1. Scans the raw buffer for the start of a JSON object ('{').
 *  2. Extracts and parses the JSON body — expected fields:
 *       - "people" (int)  : current total people count at the entrance.
 *       - "event"  (str)  : "ENTER" or "EXIT" (defaults to "ENTER" if absent).
 *  3. Builds a DataPacket tagged source="coap", zone="entrance" and pushes
 *     it onto dataQueue. The event string is stored in packet.payload so
 *     processingTask() can determine flow direction.
 *  4. Sends a 4-byte CoAP ACK back to the sender so it stops retransmitting.
 *
 * @param pvParameters Unused (required by FreeRTOS task signature).
 */
  void coapTask(void *pvParameters) {
    while (true) {
      int packetSize = udp.parsePacket();
      if (packetSize) {
        byte buffer[512];
        int len = udp.read(buffer, 512);

        Serial.println("\n[COAP] Packet received");

        // Find JSON start — locate '{' in buffer
        int jsonStart = -1;
        for (int i = 0; i < len; i++) {
          if (buffer[i] == '{') { jsonStart = i; break; }
        }

        if (jsonStart < 0) {
          Serial.println("[COAP] No JSON found in packet");
          vTaskDelay(10 / portTICK_PERIOD_MS);
          continue;
        }

        String payload = "";
        for (int i = jsonStart; i < len; i++) {
          payload += (char)buffer[i];
        }
        payload.trim();

        Serial.println("[COAP Payload] " + payload);

        // Parse JSON
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
          Serial.println("[COAP] JSON parse failed: " + String(err.c_str()));
          vTaskDelay(10 / portTICK_PERIOD_MS);
          continue;
        }

        int         people = doc["people"] | 0;
        const char* event  = doc["event"]  | "ENTER";
        float       temp   = doc["t"]      | 0.0f;

        Serial.print("[COAP] event="); Serial.print(event);
        Serial.print(" people=");      Serial.print(people);
        Serial.print(" t=");           Serial.println(temp, 1);

        // Build DataPacket — event string goes into payload field so
        // processingTask() can read it to set lastFlow (+1 ENTER / -1 EXIT)
        DataPacket packet;
        packet.confidence = 1;
        packet.pir        = 0;
        packet.humidity   = 0.0f;
        packet.sound[0]   = '\0';
        strncpy(packet.source, "coap", sizeof(packet.source) - 1);
        packet.source[sizeof(packet.source) - 1] = '\0';
        strncpy(packet.zone, "entrance", sizeof(packet.zone) - 1);
        packet.zone[sizeof(packet.zone) - 1] = '\0';
        packet.count = people;
        packet.temp  = temp;
        strncpy(packet.payload, event, sizeof(packet.payload) - 1);
        packet.payload[sizeof(packet.payload) - 1] = '\0';

        if (xQueueSend(dataQueue, &packet, pdMS_TO_TICKS(200)) != pdPASS) {
          Serial.println("[COAP] Queue full — packet dropped");
        }

        // Send a minimal CoAP ACK (RFC 7252 §4.2) to stop the sender retransmitting.
        //
        // CoAP ACK frame layout (4 bytes):
        //   Byte 0 : Ver=1 (bits 7–6 = 0b01), Type=ACK (bits 5–4 = 0b10), TKL=0 (bits 3–0)
        //            → (1 << 6) | (2 << 4) = 0x60
        //   Byte 1 : Code 2.04 Changed = 0x44  (class 2, detail 4)
        //   Bytes 2–3 : Message ID mirrored from the incoming request so the
        //               sender can match this ACK to the original message. 
        byte ack[4];
        ack[0] = (1 << 6) | (2 << 4);
        ack[1] = 0x44;
        ack[2] = buffer[2];
        ack[3] = buffer[3];

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(ack, 4);
        udp.endPacket();

        Serial.println("[COAP] ACK sent ✓");
      }

      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }