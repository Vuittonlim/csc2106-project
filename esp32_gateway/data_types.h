#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>   
struct DataPacket {
  char  source[16];
  char  zone[32];
  int   count;
  int   confidence;  ///< Zone 2 only: 1 = sound+BLE agree, 0 = disagree
  int   pir;         ///< Zone 1 only: PIR motion value (1 = motion, 0 = none)
  float temp;
  float humidity;    ///< Zone 1 only: relative humidity in %
  char  sound[16];   ///< Zone 2 only: raw sound classification ("Low"/"Medium"/"High")
                     ///< Used in d2 blend when confidence == 0
  char  payload[256];///< Zone 1: level label from "s" (LoRa) or "c" (MQTT fallback)
                     ///< Zone 2: pre-fused crowd label from MQTT "crowd" field
                     ///< CoAP  : event string ("ENTER"/"EXIT")
};

#endif