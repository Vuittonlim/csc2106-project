#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>   
struct DataPacket {
  String source;
  String zone;
  int count;
};

#endif