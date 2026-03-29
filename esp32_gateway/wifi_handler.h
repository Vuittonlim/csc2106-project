#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <Arduino.h>

void setupMQTT();
void mqttTask(void *pvParameters);
void handleMQTT();
void publishAnalytics(String payload);

#endif