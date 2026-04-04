#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <WiFi.h>

// WiFi credentials
const char* ssid     = "";
const char* password = "";

void setupHTTP() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void handleHTTP() {
  server.handleClient();
}

#endif