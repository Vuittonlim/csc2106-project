#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <WiFi.h>
#include <WebServer.h>

//  WiFi credentials
const char* ssid = "";
const char* password = "";


#define REFRESH_INTERVAL 5  // seconds, change to 300 for production (5 mins)

WebServer server(80);

extern String currentSoundLevel;
extern int bleCount;
extern String currentCrowdLabel;

//  check if request is from browser
bool isFromBrowser() {
  String userAgent = server.header("User-Agent");
  return userAgent.indexOf("Mozilla") >= 0;
}

//  /crowd endpoint
void handleCrowdRequest() {
  unsigned long t = millis();
  String soundFirst = currentSoundLevel.substring(0,1);
  String crowdFirst = currentCrowdLabel.substring(0,1);

  if (isFromBrowser()) {
    //  HTML response for browser
    String crowdColor = "green";
    if (currentCrowdLabel == "Medium") crowdColor = "orange";
    else if (currentCrowdLabel == "High") crowdColor = "red";

    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='" + String(REFRESH_INTERVAL) + "'>";
    html += "<title>M5Crowd Sensor</title>";
    html += "<style>";
    html += "body { font-family: Arial; text-align: center; padding: 40px; background: #111; color: white; }";
    html += "h1 { font-size: 2em; }";
    html += ".crowd { font-size: 4em; font-weight: bold; color: " + crowdColor + "; }";
    html += ".info { font-size: 1.2em; margin: 10px; color: #aaa; }";
    html += ".timestamp { font-size: 0.8em; color: #555; margin-top: 30px; }";
    html += "</style></head><body>";
    html += "<h1> Canteen Crowd Monitor</h1>";
    html += "<div class='crowd'>" + currentCrowdLabel + "</div>";
    html += "<div class='info'> Sound: " + currentSoundLevel + "</div>";
    html += "<div class='info'> BLE Devices: " + String(bleCount) + "</div>";
    html += "<div class='timestamp'>Last updated: " + String(t) + "ms uptime</div>";
    html += "<div class='timestamp'>Auto-refreshes every " + String(REFRESH_INTERVAL) + " seconds</div>";
    html += "</body></html>";

    server.send(200, "text/html", html);
    Serial.println("HTML served to browser");

  } else {
    // JSON response for ESP32
    if (currentCrowdLabel == "") {
      server.send(503, "application/json", "{\"error\":\"sensor not ready\"}");
      return;
    }
    String json = "{\"s\":\"" + soundFirst +
              "\",\"b\":" + String(bleCount) +
              ",\"c\":\"" + crowdFirst +
              "\",\"t\":" + String(t) + "}";
    server.send(200, "application/json", json);
    Serial.println("JSON served: " + json);
  }
  Serial.println("Request from: " + server.client().remoteIP().toString());
}

// root endpoint
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>M5Crowd</title>";
  html += "<style>body { font-family: Arial; text-align: center; padding: 40px; background: #111; color: white; }</style>";
  html += "</head><body>";
  html += "<h1>M5Crowd Sensor Node</h1>";
  html += "<p>Available endpoints:</p>";
  html += "<p><a href='/crowd' style='color: #4af;'>GET /crowd</a> — crowd status</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

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

  server.on("/", handleRoot);
  server.on("/crowd", HTTP_GET, handleCrowdRequest);
  const char* headerKeys[] = {"User-Agent"};
  server.collectHeaders(headerKeys, 1);
  server.begin();
  Serial.println("HTTP server started");
}

void handleHTTP() {
  server.handleClient();
}

#endif
