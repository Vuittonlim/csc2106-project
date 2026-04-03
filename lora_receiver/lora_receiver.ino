/**
 * @file lora_receiver.ino
 * @brief Arduino Uno LoRa receiver — reads incoming LoRa packets and forwards
 *        them to the ESP32 over UART (Serial at 115200 baud).
 *
 * Acts as a radio bridge: the RFM95 module receives LoRa transmissions from
 * the sensor node, then this sketch immediately prints each packet to Serial
 * in the format that lora_handler.cpp on the ESP32 expects:
 *
 *   "RSSI: -61 | 183|{json}"
 *
 * Hardware connections (Arduino Uno → RFM95):
 *   Pin 10 → NSS (chip select)
 *   Pin 9  → RST (reset)
 *   Pin 2  → DIO0 (interrupt)
 *   SPI    → SCK/MOSI/MISO (hardware SPI)
 */


// --- RFM95 pin assignments ---
#include <RH_RF95.h>  // RadioHead RFM95 LoRa drive
#include <SPI.h>      // SPI bus — required by RH_RF95

#define RFM95_CS 10
#define RFM95_RST 9
#define RFM95_INT 2
#define RF95_FREQ 915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);

/**
 * @brief One-time hardware initialisation.
 *
 * Performs a manual hardware reset of the RFM95 before initialising the
 * RadioHead driver. Radio parameters must exactly match the sender node or
 * packets will not be received.
 */
void setup() {
  // 115200 baud matches lora_handler.cpp on the ESP32 (Serial2.begin(115200))
  Serial.begin(115200);  
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);

 // Initialise the RadioHead RFM95 driver over SPI
  if (!rf95.init()) {
    Serial.println("LoRa init FAIL");
    while (1);
  }
  // Set operating frequency — must match the sender exactly
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("Freq FAIL");
    while (1);
  }

  // Match sender settings exactly
  rf95.setSpreadingFactor(7);
  rf95.setSignalBandwidth(125000);
  rf95.setCodingRate4(5);

  Serial.println("Receiver Ready\n");
}

/**
 * @brief Main loop — poll for incoming LoRa packets and forward over Serial.
 *
 * RadioHead's available() checks the DIO0 interrupt flag. When a packet has
 * arrived, recv() copies it into buf and sets len to the actual byte count.
 * The packet is printed in the format lora_handler.cpp expects:
 *
 *   "RSSI: -61 | {raw message}"
 *
 * The ESP32's lora_handler.cpp then parses this line, extracts the RSSI and
 * the JSON body, and enqueues a DataPacket for processingTask().
 */
void loop() {
  if (rf95.available()) {       // True when a complete packet has been received
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);

    if (rf95.recv(buf, &len)) {      
      if (len >= RH_RF95_MAX_MESSAGE_LEN) len = RH_RF95_MAX_MESSAGE_LEN - 1;
      buf[len] = '\0';

      // Forward to ESP32 over UART in the format lora_handler.cpp expects:
      // "RSSI: <value> | <message>"
      Serial.print("RSSI: ");
      Serial.print(rf95.lastRssi());
      Serial.print(" | ");
      Serial.println((char*)buf);
    } else {
      Serial.println("Recv failed"); // tells you if available() triggered but recv() didn't
    }
  }
}
