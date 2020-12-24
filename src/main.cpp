#include <Arduino.h>
#define ANA A0
#define DIGI D5
#define PUMPOUT D6
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PID_v1.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#ifndef STASSID
#define STASSID "MYSSID"
#define STAPSK "MYPASSWORD"
#endif

ESP8266WebServer server(80);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

const char *ssid = STASSID;
const char *password = STAPSK;

double analogValue = 0.0;
long previousMeasurementMillis = 0;

double setPoint = 400;
int highValue = 400;
int lowValue = 200;
int offset = 0;

double Input, Output;
PID myPID(&Input, &Output, &setPoint, 1, 10, 2, DIRECT);
int WindowSize = 5000;
unsigned long windowStartTime;

const String postForms = "<html>\
  <head>\
    <title>ESP8266 Web Server POST handling</title>\
    <style>\
      body { background-color: #99FF66; font-family: Arial, Helvetica, Sans-Serif; Color: #996633; }\
    </style>\
  </head>\
  <body>\
    <h1> Current humidity: {{humidity}} </h1> \
    <h1> Current time: {{time}} </h1> \
    <h2> High humidity setting: {{highValue}} </h2> \
    <h2> Low  humidity setting: {{lowValue}} </h2> \
    <h1>Change settings for humidity</h1><br>\
    <form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/settings/\">\
      <label for=\"highValue\"> High value </label> \
      <input value=\"{{highValue}}\" id=\"highValue\" type=\"text\" name='highValue'><br>\
      <label for=\"lowValue\"> Low value </label> \
      <input value=\"{{lowValue}}\" id=\"lowValue\" type=\"text\" name='lowValue'><br>\
      <label for=\"offset\"> Timezone offset in hours </label> \
      <input value=\"{{offset}}\" id=\"offset\" type=\"text\" name='offset'><br>\
      <input type=\"submit\" value=\"Submit\">\
    </form>\
  </body>\
</html>";

void writeIntToEeprom(int address, int number)
{
  byte byte1 = number >> 8;
  byte byte2 = number & 0xFF;
  EEPROM.write(address, byte1);
  EEPROM.write(address + 1, byte2);
}

int readIntFromEeprom(int address)
{
  byte byte1 = EEPROM.read(address);
  byte byte2 = EEPROM.read(address + 1);
  return (byte1 << 8) + byte2;
}

void loadSettingsFromEeprom()
{
  highValue = readIntFromEeprom(0);
  lowValue = readIntFromEeprom(sizeof(highValue));
  timeClient.setTimeOffset(readIntFromEeprom(sizeof(highValue) + sizeof(lowValue)) * 3600);
  offset = readIntFromEeprom(sizeof(highValue) + sizeof(lowValue));
}

void handleRoot()
{
  String body = String(postForms);
  body.replace("{{humidity}}", String(1024 - analogValue, 2));
  body.replace("{{highValue}}", String(highValue));
  body.replace("{{lowValue}}", String(lowValue));
  body.replace("{{offset}}", String(offset));
  body.replace("{{time}}", timeClient.getFormattedTime());
  server.send(200, "text/html", body);
}

void handleSettings()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
  }
  else
  {
    highValue = server.arg("highValue").toInt();
    lowValue = server.arg("lowValue").toInt();

    offset = server.arg("offset").toInt();
    timeClient.setTimeOffset(offset * 3600);

    writeIntToEeprom(0, highValue);
    writeIntToEeprom(sizeof(highValue), lowValue);
    writeIntToEeprom(sizeof(highValue) + sizeof(lowValue), offset);
    EEPROM.commit();

    //Set redirect location and sent redirect to get back to root
    server.sendHeader("Location", String("/"), true);
    server.send(302, "text/plain", "");
    handleRoot();
  }
}

void handleForm()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
  }
  else
  {
    String message = "POST form was:\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(200, "text/plain", message);
  }
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup()
{
  // put your setup code here, to run once:
  pinMode(ANA, INPUT);
  pinMode(DIGI, INPUT);
  pinMode(PUMPOUT, OUTPUT);
  Serial.begin(115200);
  EEPROM.begin(512);
  ESP.wdtEnable(1000);
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("  "))
  {
    Serial.println("MDNS responder started");
  }

  timeClient.begin();

  analogValue = analogRead(ANA);
  server.on("/", handleRoot);

  server.on("/settings/", handleSettings);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
  loadSettingsFromEeprom();
  windowStartTime = millis();
  myPID.SetOutputLimits(0, WindowSize);
  myPID.SetMode(AUTOMATIC);
}

void loop()
{
  unsigned long now = millis();
  server.handleClient();
  timeClient.update();

  int currentHour = timeClient.getHours();
  if (currentHour > 9 && currentHour < 21)
  {
    setPoint = highValue;
  }
  else
  {
    setPoint = lowValue;
  }

  // update every second
  if (now - previousMeasurementMillis > 1000)
  {
    analogValue = analogRead(ANA);
    Input = 1024 - analogValue;
    previousMeasurementMillis = now;
  }

  myPID.Compute();

  /************************************************
     turn the output pin on/off based on pid output,
     ignore output smaller than 1 second to save pump
   ************************************************/
  if (now - windowStartTime > WindowSize)
  { //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  if (Output > now - windowStartTime && Output > 1000)
  {
    digitalWrite(PUMPOUT, HIGH);
  }
  else
    digitalWrite(PUMPOUT, LOW);
}
