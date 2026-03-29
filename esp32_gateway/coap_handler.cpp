#include "coap_handler.h"
#include "data_types.h"
#include <WiFiUdp.h>
#include <WiFiUdp.h>
#include "data_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern QueueHandle_t dataQueue;

WiFiUDP udp;
const int COAP_PORT = 5683;

void setupCoAP() {

  udp.begin(COAP_PORT);

  Serial.print("Listening on port: ");
  Serial.println(COAP_PORT);
  Serial.println("CoAP server started");
}

void coapTask(void *pvParameters) {

  while (true) {

    int packetSize = udp.parsePacket();

    if (packetSize) {

      byte buffer[512];
      int len = udp.read(buffer, 512);

      Serial.println("\n[COAP] Packet received");

      // Extract payload
      String payload = "";

      for (int i = 4; i < len; i++) {
        if (buffer[i] == 0xFF && i + 1 < len) {
          for (int j = i + 1; j < len; j++) {
            payload += (char)buffer[j];
          }
          break;
        }
      }

      Serial.println("[COAP Payload] " + payload);

      // Convert to DataPacket
      DataPacket packet;
      packet.source = "coap";
      packet.zone = "entrance";
      packet.count = payload.toInt();

      // Send to queue
      xQueueSend(dataQueue, &packet, portMAX_DELAY);

      //  Send ACK 
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