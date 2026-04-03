#ifndef LORA_HANDLER_H
#define LORA_HANDLER_H

#include <Arduino.h>
#include "data_types.h"

// UART pins
#define RXD2 44
#define TXD2 43

// Function declarations
void initLoRaUART();
void loraTask(void *pvParameters);

// External queue (shared from main)
extern QueueHandle_t dataQueue;

#endif