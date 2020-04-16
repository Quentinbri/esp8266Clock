#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>
#include <string>

// Server
ESP8266WebServer webServer;
WiFiClient       wifiClient;
AutoConnect      acPortal(webServer);
AutoConnectAux   acClockConfig;
ACRadio(acClockConfigOrientation, {"Cable on left", "Cable on right"}, "Orientation", AC_Vertical, 1);

// Time client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, "pool.ntp.org");
String formattedTime;
const char worldTimeApiURL[] = "http://worldtimeapi.org/api/ip";
const size_t worldTimeApiJsonCapacity = JSON_OBJECT_SIZE(15) + 290;
DynamicJsonDocument worldTimeApiJSON(worldTimeApiJsonCapacity);
bool offsetNeedUpdate = false;

// Display
const int CLK = D6;
const int DIO = D5;
TM1637Display display(CLK, DIO);
enum t_displayOrientation {
  normal,
  rotate
};
t_displayOrientation displayOrientation = normal;

static const char AUX_CLOCKCONFIG[] PROGMEM = R"(
{
  "title": "Clock Config",
  "uri": "/clock_config",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Configuration of the clock",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "orientation",
      "type": "ACSelect",
      "label": "Select Orientation",
      "option": ["Default","Inverted"],
      "selected": 0
    },
    {
      "name": "newline",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "apply",
      "type": "ACSubmit",
      "value": "Apply",
      "uri": "/apply"
    }
  ]
}
)";

void rootPage() {
  String content =
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<script type=\"text/javascript\">"
    "setTimeout(\"location.reload()\", 1000);"
    "</script>"
    "</head>"
    "<body>"
    "<h2 align=\"center\" style=\"color:blue;margin:20px;\">esp8266Clock</h2>"
    "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
    "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
    "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
    "</body>"
    "</html>";
  String truncatedTime = formattedTime.substring(0,5);
  content.replace("{{DateTime}}", truncatedTime);
  webServer.send(200, "text/html", content);
}

void applyPage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  //String  tz = webServer.arg("orientation");
  String selectedOrientation = webServer.arg("orientation");

  if(selectedOrientation == "Default")
  {
    displayOrientation = normal;
  }
  else if(selectedOrientation == "Inverted")
  {
    displayOrientation = rotate;
  }

  webServer.sendHeader("Location", String("http://") + webServer.client().localIP().toString() + String("/"));
  webServer.send(302, "text/plain", "");
  webServer.client().flush();
  webServer.client().stop();
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

uint8_t rotateDigit(uint8_t digit) {
  // XGFEDCBA
  bool segX = SEG_DP & digit;
  bool segG = SEG_G & digit;
  bool segF = SEG_F & digit;
  bool segE = SEG_E & digit;
  bool segD = SEG_D & digit;
  bool segC = SEG_C & digit;
  bool segB = SEG_B & digit;
  bool segA = SEG_A & digit;

  uint8_t rotatedDigit = segX * SEG_DP + segG * SEG_G
    + segC * SEG_F + segB * SEG_E
    + segA * SEG_D + segF * SEG_C
    + segE * SEG_B + segD * SEG_A;

  return rotatedDigit;
}

void displayTime() {
  int minutes = ntpClient.getMinutes();
  int hours = ntpClient.getHours();

  int hourDigits[4] = {hours/10, hours%10, minutes/10, minutes%10};
  uint8_t hourDigitsSegments[4];
  for(int i=0 ; i<4 ; i++)
  {
    if(displayOrientation == normal)
    {
      hourDigitsSegments[i] = display.encodeDigit((uint8_t)hourDigits[i]);
      //hourDigitsSegments[i] = SEG_DP | hourDigitsSegments[i];
    }
    else if(displayOrientation == rotate)
    {
      hourDigitsSegments[3-i] = rotateDigit(display.encodeDigit((uint8_t)hourDigits[i]));
      //hourDigitsSegments[3-i] = SEG_DP | hourDigitsSegments[3-i];
    }
  }
  hourDigitsSegments[1] |= SEG_DP;
  display.setSegments(hourDigitsSegments);
}

void setup() {
  Serial.begin(115200);

  webServer.on("/", rootPage);
  webServer.on("/apply", applyPage);

  // AutoConnect
  AutoConnectConfig acConfig;
  acConfig.apid = "WiFi_Clock";
  acConfig.psk = "WiFi_Clock";
  acConfig.title = "WiFi Clock";
  //acConfig.homeUri = "/home";
  acPortal.config(acConfig);
  acClockConfig.load(AUX_CLOCKCONFIG);
  acPortal.join(acClockConfig);

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
  formattedTime = ntpClient.getFormattedTime();
  Serial.println("Time: " + formattedTime);

  // Get and set the time offset the first minute of each hour
  if(offsetNeedUpdate && ntpClient.getMinutes() == 0)
  {
    offsetNeedUpdate = !updateTimeOffset();
  }
  else if(ntpClient.getMinutes() > 0)
  {
    offsetNeedUpdate = true;
  }
  
  displayTime();

  delay(1000);
}