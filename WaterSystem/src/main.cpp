#include "Arduino.h"
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>

#include "secrets.h"

#define PUMP_PIN                D0
#define WATER_SENSOR_PIN_VCC    D1
#define WATER_SENSOR_PIN_INPUT  A0

#define WATER_CHECK_INTERVAL_MS 5000

static void handleRoot();
static void handleNotFound();
static void handlePumpOn();
static void handlePumpOff();
static void connectWifi();
static void setPumpEnabled(bool enabled);
static bool isWaterDetected();
static void abortWatering();

WiFiClient wifi;
static ESP8266WebServer server(80);
Ticker ticker;
 
bool isPumpRunning = false;
bool isThereWater = false;
unsigned long lastWaterCheck;

void setup()
{
  Serial.begin(9600);
  ArduinoOTA.setHostname("autogarden");
  ArduinoOTA.onStart([]() {
    abortWatering();
    delay(100);
    Serial.println("Beginning FW update over OTA");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("Finished FW update over OTA");
  });

  connectWifi();

  if (MDNS.begin("autogarden")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);
  server.on("/pump/on", handlePumpOn);
  server.on("/pump/off", handlePumpOff);

  server.onNotFound(handleNotFound);
  server.begin();

  pinMode(WATER_SENSOR_PIN_INPUT, INPUT);
  pinMode(WATER_SENSOR_PIN_VCC, OUTPUT);
  digitalWrite(WATER_SENSOR_PIN_VCC, LOW);
  isThereWater = isWaterDetected();
  lastWaterCheck = millis();

  pinMode(PUMP_PIN, OUTPUT);
  setPumpEnabled(false);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED) {
    setPumpEnabled(false);
    connectWifi();
  } else {
    ArduinoOTA.handle();
    server.handleClient();
    if (lastWaterCheck + WATER_CHECK_INTERVAL_MS <= millis()) {
      isThereWater = isWaterDetected();
      // Abort ASAP
      if (!isThereWater && isPumpRunning) {
        abortWatering();
      }
      lastWaterCheck = millis();
    }
  }
}

static void connectWifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  ArduinoOTA.begin();
}

static void setPumpEnabled(bool enabled) {
  Serial.printf("Setting pump enabled to %u\n", enabled);
  isPumpRunning = enabled;
  if (enabled) {
    digitalWrite(PUMP_PIN, LOW);
  } else {
    digitalWrite(PUMP_PIN, HIGH);
  }
}

/*
* /pump/on?timeout=timeInSeconds
* Turn pump on for x seconds. Max 20s.
*/
static void handlePumpOn() {
  int timeout = server.arg("timeout").toInt();
  if (timeout > 0 && timeout <= 20) {
    if (!isPumpRunning && isThereWater) {
      server.send(200, "text/plain", "{success: true}");
      setPumpEnabled(true);
      ticker.once_ms(timeout * 1000, setPumpEnabled, false);
    } if (!isThereWater) {
      server.send(200, "text/plain", "{success: false, message: No water detected, fill before using}");
    } else {
      server.send(200, "text/plain", "{success: false, message: Pump is already running}");
    }

  } else {
    server.send(200, "text/plain", "{success: false, message: Invalid params}");
  }
}

/*
* /pump/off
* Turn pump off
*/
static void handlePumpOff() {
  abortWatering();
  server.send(200, "text/plain", "{success: true}");
}

static void handleRoot() {
  server.send(200, "text/plain", "Watering system!");
}

static void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

static void abortWatering() {
  ticker.detach();
  setPumpEnabled(false);
}

static bool isWaterDetected() {
  digitalWrite(WATER_SENSOR_PIN_VCC, HIGH);
  delay(200);
  int waterVoltage = analogRead(WATER_SENSOR_PIN_INPUT);
  digitalWrite(WATER_SENSOR_PIN_VCC, LOW);
  Serial.printf("Water analog reading: %d\n", waterVoltage);

  if (waterVoltage >= 512) {
    return true;
  } else {
    return false;
  }
}
