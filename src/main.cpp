#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

ESP8266WebServer Server;
AutoConnect      Portal(Server);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const size_t jsonParserCapacity = JSON_OBJECT_SIZE(15) + 290;
DynamicJsonDocument jsonDoc(jsonParserCapacity);

bool offsetNeedUpdate = false;

void rootPage() {
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}

bool updateOffset()
{
  bool offsetUpdated = false;
  WiFiClient client;
  HTTPClient http;
  if (http.begin(client, "http://worldtimeapi.org/api/ip"))
  {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
    {
      String payload = http.getString();
      // Serial.println(payload);
      deserializeJson(jsonDoc, payload);
      int rawOffset = jsonDoc["raw_offset"];
      int dstOffset = jsonDoc["dst_offset"];
      timeClient.setTimeOffset(rawOffset + dstOffset);
      Serial.printf("Offset: %d\n", rawOffset + dstOffset);
      offsetUpdated = true;
    }
    else
    {
      if(httpCode > 0)
      {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      }
      else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      }
    }
  }
  else
  {
    Serial.printf("[HTTP} Unable to connect\n");
  }
  return offsetUpdated;
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  Server.on("/home", rootPage);

  AutoConnectConfig acConfig;

  acConfig.apid = "WiFi_Clock";
  acConfig.psk = "WiFi_Clock";
  acConfig.title = "WiFi Clock";
  acConfig.homeUri = "/home";
  Portal.config(acConfig);

  if (Portal.begin()) {
    Serial.println("HTTP server:" + WiFi.localIP().toString());
    timeClient.begin();
    while(!updateOffset())
    {
      delay(1000);
    }
  }
}

void loop() {
  Portal.handleClient();
  timeClient.update();

  if(offsetNeedUpdate && timeClient.getMinutes() == 0)
  {
    bool offsetUpdated = updateOffset();
    if(offsetUpdated)
    {
      offsetNeedUpdate = false;
    }
  }
  else if(timeClient.getMinutes() > 0)
  {
    offsetNeedUpdate = true;
  }
  
  Serial.print(timeClient.getHours());
  Serial.print(":");
  Serial.print(timeClient.getMinutes());
  Serial.print(":");
  Serial.println(timeClient.getSeconds());

  delay(1000);
}