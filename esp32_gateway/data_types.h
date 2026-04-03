#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>   
struct DataPacket {
  char source[16];
  char zone[32];
  int count;
  float temp;
  char payload[256];
};

#endif