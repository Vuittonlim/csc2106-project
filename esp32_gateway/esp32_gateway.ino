/**
 * @file main.cpp
 * @brief Application entry point for the crowd analytics firmware.
 *
 * Initialises all hardware interfaces and communication handlers, creates the
 * shared dataQueue, then launches five concurrent FreeRTOS tasks that run
 * for the lifetime of the device. The Arduino loop() is intentionally left
 * empty — all work is done inside the tasks.
 *
 * Data flow overview:
 *
 *   [LoRa UART] ──loraTask()──┐
 *   [MQTT/Wi-Fi] ──mqttTask()──┤
 *   [BLE fallback] ─bleTask()──┼──► dataQueue ──► processingTask() ──► MQTT publish
 *   [CoAP UDP] ──coapTask()───┘
 */
#include "config.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "coap_handler.h"
#include "processor.h"
#include "data_types.h"
#include "lora_handler.h"
#include <ArduinoJson.h>

/// Shared queue that all sensor tasks write to and processingTask() reads from.
/// Holds up to 20 DataPackets; each slot is sizeof(DataPacket) bytes.
QueueHandle_t dataQueue;

/// Global fallback flag — set true by bleTask() when Zone 2 MQTT goes silent,
/// cleared when MQTT resumes. Read by wifi_handler.cpp to tag packets correctly.
bool useFallback = false;

/**
 * @brief One-time hardware and software initialisation.
 *
 * Execution order matters here:
 *  1. Serial must start first for all subsequent debug output to be visible.
 *  2. LoRa UART is initialised before the queue so the serial port is ready.
 *  3. dataQueue is created before any tasks are started so no task races to
 *     use it before it exists.
 *  4. Peripheral setup (MQTT/Wi-Fi, BLE, CoAP) runs next — each blocks only
 *     briefly and does not depend on the others being ready.
 *  5. Tasks are created last; they begin executing immediately after creation
 *     if their priority is higher than the current context (it isn't here —
 *     all tasks are priority 1).
 */
void setup() {
  Serial.begin(115200);
  initLoRaUART();

  // Create the central data queue — must initialise first
  dataQueue = xQueueCreate(20, sizeof(DataPacket));

 // Initialise communication peripherals
  setupMQTT();
  setupBLE();
  setupCoAP();
 

 // Launch sensor ingestion tasks — each feeds DataPackets into dataQueue
  xTaskCreate(bleTask, "BLE Task", 4096, NULL, 1, NULL);
  xTaskCreate(coapTask, "CoAP Task", 4096, NULL, 1, NULL);
  xTaskCreate(mqttTask, "MQTT Task", 8192, NULL, 1, NULL);
  xTaskCreate(loraTask, "LoRa Task", 4096, NULL, 1, NULL);
  xTaskCreate(processingTask, "Processing Task", 4096, NULL, 1, NULL);
}

/**
 * @brief Arduino main loop — intentionally empty.
 *
 * All application logic runs inside FreeRTOS tasks created in setup().
 * The scheduler preempts this loop and runs the tasks concurrently, so
 * there is nothing to do here.
 */
void loop() {
  // leave empty (FreeRTOS handles everything)
}