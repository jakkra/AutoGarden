#include <SPI.h>
#include <RH_NRF24.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "secrets.h"

#define DEBUG

#define NUM_ACKS_TO_SEND        100

#define DUPLICATE_FILTER_LENGTH 10

#ifdef DEBUG
#define LOG(msg) Serial.print(msg)
#define LOGF Serial.printf
#define LOGLN Serial.println
#else
#define LOG(msg)
#define LOGF(...)
#define LOGLN (...)
#endif

RH_NRF24 nrf24(2, 4);

enum MeasurementType {
  TEMPERATURE,
  MOISTURE,
  ACK
};

typedef struct Payload {
  uint16_t    id;
  uint16_t    type;
  uint16_t    value;
  uint16_t    uuidIsh;
} Payload;

const char* moistureUrl = "http://207.154.239.115/api/moisture";
const char* temperatureUrl = "http://207.154.239.115/api/temperature";

uint16_t lastUUIDs[DUPLICATE_FILTER_LENGTH];
uint8_t currentUUID = 0;

void setup() 
{
#ifdef DEBUG
  Serial.begin(9600);
#endif
  if (!nrf24.init()) {
    LOGLN("init failed");
  }
  if (!nrf24.setChannel(1)) {
    LOGLN("setChannel failed");
  }
  if (!nrf24.setRF(RH_NRF24::DataRate250kbps, RH_NRF24::TransmitPower0dBm)) {
    LOGLN("setRF failed");
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    LOG("Connection Failed! Rebooting...\n");
    delay(5000);
    ESP.restart();
  }
}

void sendMoistureLevel(uint16_t id, uint16_t level) {
  HTTPClient http;
  int httpCode;
  char jsonData[50];

  uint16_t analogValue = level;

  LOGF("Moisture Level %d\n", analogValue);
  
  http.begin(moistureUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-access-token", accessToken);
  sprintf(jsonData, "{\"moisture\": %d, \"name\": %d}", analogValue, id);
  LOG(jsonData);
  httpCode = http.POST(jsonData);
  if (httpCode > 0) {
    LOGF("\n[HTTP] POST... code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      LOG(payload);
    }
  } else {
    LOGF("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
}

void sendTemperature(uint16_t id, uint16_t temperature) {
  HTTPClient http;
  int httpCode;
  char jsonData[50];


  LOGF("Temperature %d\n", temperature);
  
  http.begin(temperatureUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-access-token", accessToken);
    sprintf(jsonData, "{\"temperature\": %d, \"name\": \"%d\"}", temperature, id);
    LOG(jsonData);
    httpCode = http.POST(jsonData);
    if (httpCode > 0) {
      LOGF("\n[HTTP] POST... code: %d\n", httpCode);
      
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        LOG(payload);
      }
    } else {
      LOGF("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());    
    }
}

static void sendAck(uint16_t id) {
  
  Payload data;
  data.id = id;
  data.type = ACK;
  data.value = 0;

  for(uint8_t i = 0; i < NUM_ACKS_TO_SEND; i++) {
    nrf24.send((uint8_t*)&data, sizeof(data));
    nrf24.waitPacketSent();
  }
}

static bool isDuplicate(uint16_t uuid) {
  
  for(uint8_t i = 0; i < DUPLICATE_FILTER_LENGTH; i++) {
    if (lastUUIDs[i] == uuid) {
      return true;
    }
  }
  LOGLN("");
  lastUUIDs[currentUUID] = uuid;
  currentUUID = (currentUUID + 1) % DUPLICATE_FILTER_LENGTH;
  return false;
  
}

void loop()
{
  if (nrf24.available()) {
    uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (nrf24.recv(buf, &len)) {
      if (len == sizeof(Payload)) {
        Payload* data = (Payload*)buf;
        //LOGF("Received from id: %d, type: %d, value: %d, uuidIsh: %d\n", data->id, data->type, data->value, data->uuidIsh);
        sendAck(data->id);
        if (!isDuplicate(data->uuidIsh)) {
          if (data->type == MOISTURE) {
            sendMoistureLevel(data->id, data->value);
          } else if(data->type == TEMPERATURE) {
            sendTemperature(data->id, data->value);
          }
        }
      } else {
        LOGF("Unexpected message length, expected %d, but was %d\n", sizeof(Payload), len);
      }
    }
    else {
      LOGLN("recv failed");
    }
  }
}

