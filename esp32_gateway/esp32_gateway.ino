#include "config.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "coap_handler.h"
#include "processor.h"
#include "data_types.h"

#include <ArduinoJson.h>


QueueHandle_t dataQueue;
bool useFallback = false;



void setup() {
  Serial.begin(115200);
  dataQueue = xQueueCreate(20, sizeof(DataPacket));

  setupMQTT();
  setupBLE();
  setupCoAP();
 

  xTaskCreate(wifiTask, "WiFi Task", 4096, NULL, 1, NULL);
  xTaskCreate(bleTask, "BLE Task", 4096, NULL, 1, NULL);
  xTaskCreate(coapTask, "CoAP Task", 4096, NULL, 1, NULL);
  xTaskCreate(mqttTask, "MQTT Task", 8192, NULL, 1, NULL);
  xTaskCreate(processingTask, "Processing Task", 4096, NULL, 1, NULL);
}

void wifiTask(void *pvParameters) {
  while (true) {
    handleMQTT();
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void loop() {
  // leave empty (FreeRTOS handles everything)
}