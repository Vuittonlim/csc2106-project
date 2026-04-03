#include <RH_RF95.h>
#include <SPI.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <Wire.h>

#define RFM95_CS 10
#define RFM95_RST 9
#define RFM95_INT 2
#define RF95_FREQ 915.0
#define OLED_ADDRESS 0x3C

RH_RF95 rf95(RFM95_CS, RFM95_INT);
SSD1306AsciiWire oled;

void setup() {
  Serial.begin(9600);
  Wire.begin();
  
  oled.begin(&Adafruit128x64, OLED_ADDRESS);
  oled.setFont(System5x7);
  oled.clear();
  oled.println("Initializing...");

  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  if (!rf95.init()) {
    oled.clear();
    oled.println("LoRa init FAIL");
    while (1);
  }

  if (!rf95.setFrequency(RF95_FREQ)) {
    oled.clear();
    oled.println("Freq set FAIL");
    while (1);
  }

  rf95.setTxPower(13, false);
  oled.clear();
  oled.println("LoRa ready");
}

void loop() {
  if (Serial.available()) {
    String payload = Serial.readStringUntil('\n');
    payload.trim();
    
    if (payload.length() > 0) {
      rf95.send((uint8_t *)payload.c_str(), payload.length());
      rf95.waitPacketSent();
      
      oled.clear();
      oled.println("Sent:");
      oled.println(payload);
    }
  }
}
