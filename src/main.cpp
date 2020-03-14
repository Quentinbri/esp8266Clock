#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>

// Server
ESP8266WebServer webServer;
AutoConnect      acPortal(webServer);
WiFiClient       wifiClient;

// Time client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, "pool.ntp.org");
const char worldTimeApiURL[] = "http://worldtimeapi.org/api/ip";
const size_t worldTimeApiJsonCapacity = JSON_OBJECT_SIZE(15) + 290;
DynamicJsonDocument worldTimeApiJSON(worldTimeApiJsonCapacity);
bool offsetNeedUpdate = false;

// Display
const int CLK = D6;
const int DIO = D5;
TM1637Display display(CLK, DIO);

void rootPage() {
  char content[] = "Hello, world";
  webServer.send(200, "text/plain", content);
}

bool updateTimeOffset()
{
  bool timeOffsetUpdated = false;
  HTTPClient worldTimeApiClient;
  if (worldTimeApiClient.begin(wifiClient, worldTimeApiURL))
  {
    int httpCode = worldTimeApiClient.GET();
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
    {
      String worldTimeApiContent = worldTimeApiClient.getString();
      deserializeJson(worldTimeApiJSON, worldTimeApiContent);
      int rawOffset = worldTimeApiJSON["raw_offset"];
      int dstOffset = worldTimeApiJSON["dst_offset"];
      int totalOffset = rawOffset + dstOffset;
      ntpClient.setTimeOffset(totalOffset);
      Serial.printf("[HTTP] Total Offset: %d\n", totalOffset);
      timeOffsetUpdated = true;
    }
    else
    {
      if(httpCode > 0)
      {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      }
      else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", worldTimeApiClient.errorToString(httpCode).c_str());
      }
    }
  }
  else
  {
    Serial.printf("[HTTP] Unable to connect\n");
  }
  return timeOffsetUpdated;
}

void setup() {
  Serial.begin(115200);

  webServer.on("/home", rootPage);

  // AutoConnect
  AutoConnectConfig acConfig;
  acConfig.apid = "WiFi_Clock";
  acConfig.psk = "WiFi_Clock";
  acConfig.title = "WiFi Clock";
  acConfig.homeUri = "/home";
  acPortal.config(acConfig);

  // Display
  display.setBrightness(0x0a); // Maximum brightness

  if (acPortal.begin()) {
    Serial.println("Client started: " + WiFi.localIP().toString());

    // Start NTP client
    ntpClient.begin();

    // Get and set the time offset
    while(!updateTimeOffset())
    {
      delay(2000);
    }
  }
}

void loop() {
  acPortal.handleClient();

  // Update NTP time
  ntpClient.update();

  // Get and set the time offset the first minute of each hour
  if(offsetNeedUpdate && ntpClient.getMinutes() == 0)
  {
    offsetNeedUpdate = !updateTimeOffset();
  }
  else if(ntpClient.getMinutes() > 0)
  {
    offsetNeedUpdate = true;
  }
  
  Serial.println("Time: " + ntpClient.getFormattedTime());

  int time = ntpClient.getHours() * 100 + ntpClient.getMinutes();
  display.showNumberDecEx(time, 0b01000000);

  delay(1000);
}