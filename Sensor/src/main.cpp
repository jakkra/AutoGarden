#include <SPI.h>
#include <RH_NRF24.h>
#include <LowPower.h>
#include <OneWire.h>
#include <DallasTemperature.h>

typedef struct Payload {
  uint16_t    id;
  uint16_t    type;
  uint16_t    value;
  uint16_t    uuidIsh;
} Payload;

//#define DEBUG

#ifdef DEBUG
#define LOG(msg) Serial.print(msg)
#define LOGLN Serial.println
#else
#define LOG(msg)
#define LOGLN(msg)
#endif                                                                                                                                                             

// Temperature sensor can be auto detected
// Both temperature and Moisture sensor
#define MOISTURE_SENSOR_CONNECTED

#define TEMPERATURE_PRECISION             11
#define DS18B20_REQUEST_TIME_MS           375
#define ONE_WIRE_BUS_PIN                  8
#define TEMP_SENSOR_POWER_PIN             4

#define MOISTURE_SENSOR_POWER_PIN         2
#define MOISTURE_SENSOR_IN_PIN            A0

#define ITERATIONS_60_MIN_IN_8S_INTERVAL  450

#define LOG_RETRY_COUNT                   2
#define WAIT_ACK_TIMEOUT                  100
#define NODE_ID                           7

OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature sensors(&oneWire);

RH_NRF24 nrf24(9, 10);

uint8_t tempSensorAddress[8];
bool foundTempSensor = false;
uint16_t sleepCount;
uint16_t timesWokenUp = 0;

enum MeasurementType {
  TEMPERATURE,
  MOISTURE,
  ACK
};

void setup() {
#ifdef DEBUG
  Serial.begin(9600);
#endif

  uint16_t seed = analogRead(A1);
  LOG("Init random with seed: ");
  LOGLN(seed);
  randomSeed(seed);
  if (!nrf24.init()) {
    LOGLN("init failed");
  }
  if (!nrf24.setChannel(1)) {
    LOGLN("setChannel failed");
  }
  if (!nrf24.setRF(RH_NRF24::DataRate250kbps, RH_NRF24::TransmitPower0dBm)) {
    LOGLN("setRF failed");    
  }

#ifdef MOISTURE_SENSOR_CONNECTED
  pinMode(MOISTURE_SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(MOISTURE_SENSOR_POWER_PIN, LOW);
#endif
  
  // See if we can find a DS18B20
  pinMode(TEMP_SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(TEMP_SENSOR_POWER_PIN, HIGH);
  delay(50);
  sensors.begin(); 
  // Sets so requestTemperatures() does not block, meaning we can measure moisture while waiting for temperature result
  sensors.setWaitForConversion(false);
  if (sensors.getAddress(tempSensorAddress, 0)) {
    foundTempSensor = true;
    LOGLN("Found DS12b20!");
  } else {
    foundTempSensor = false;
    LOGLN("No Temperature sensor found!");
  }
  digitalWrite(TEMP_SENSOR_POWER_PIN, LOW);
  sleepCount = 0;
}

static uint16_t getRand(uint16_t value) {
  uint16_t rand = value + random(40000);
  LOG("Update random with new seed: ");
  LOGLN(rand);
  randomSeed(rand);
  return rand;
}

static boolean waitForAck() {
  long startTime = millis();
  Payload* data = NULL;
  uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  while ((millis() - startTime) < WAIT_ACK_TIMEOUT) {
    if (nrf24.available()) {
      if (nrf24.recv(buf, &len)) {
        LOGLN("Got data");
        LOGLN(len);
        if (len == sizeof(Payload)) {
          data = (Payload*)buf;
          if (data->type == ACK && data->id == NODE_ID) {
            return true;
          }
        } else {
          LOGLN("Not ACK for us");
        }
      }
    }
  }
  return false;
}

static uint16_t getMoisutureLevel() {
  digitalWrite(MOISTURE_SENSOR_POWER_PIN, HIGH);
  delay(100);
  uint16_t value = analogRead(MOISTURE_SENSOR_IN_PIN);
  LOGLN(analogRead(MOISTURE_SENSOR_IN_PIN));
  digitalWrite(MOISTURE_SENSOR_POWER_PIN, LOW);
  return 1024 - value;
}

static long startAndRequestTemperatures() {
  digitalWrite(TEMP_SENSOR_POWER_PIN, HIGH);
  delay(50);
  sensors.setResolution(tempSensorAddress, TEMPERATURE_PRECISION);
  sensors.requestTemperatures();
  return millis();
}

static uint16_t getTemperature(long timeSinceRequest) {
  float temperature = 0;
  if (timeSinceRequest < DS18B20_REQUEST_TIME_MS) {
    LOG("Sleeping to wait for temp: ");
    LOGLN(DS18B20_REQUEST_TIME_MS - timeSinceRequest);
    delay(DS18B20_REQUEST_TIME_MS - timeSinceRequest);
  }
  temperature = sensors.getTempC(tempSensorAddress);
  digitalWrite(TEMP_SENSOR_POWER_PIN, LOW);
  LOG("Temperature: ");
  LOGLN(temperature);

  return round(temperature);
}

static boolean sendData(Payload* data) {
  boolean gotAck = false;
  for(uint8_t i = 0; !gotAck && (i < LOG_RETRY_COUNT); i++) {
    LOG("Sending data try: ");
    LOGLN(i);
    nrf24.send((uint8_t*)data, sizeof(Payload));
    nrf24.waitPacketSent();
    gotAck = waitForAck();
  }
  return gotAck;
}

static void doMoisture() {
  Payload data;
  uint16_t level = getMoisutureLevel();
  LOG("Sending to moisture level: ");
  LOGLN(level);

  memset(&data, 0, sizeof(Payload));
  data.id = NODE_ID;
  data.type = MOISTURE;
  data.value = level;
  data.uuidIsh = getRand(timesWokenUp + level);
  if (!sendData(&data)) {
    LOGLN("Failed to send moisture");
  } else {
    LOGLN("Success sending moisture");
  }
}

static void doTemperature(long timeSinceRequest) {
  Payload data;
  uint16_t temperature = getTemperature(timeSinceRequest);
  LOG("Sending to temperature: ");
  LOGLN(temperature);

  memset(&data, 0, sizeof(Payload));
  data.id = NODE_ID;
  data.type = TEMPERATURE;
  data.value = temperature;
  data.uuidIsh = getRand(timesWokenUp + temperature);
  if (!sendData(&data)) {
    LOGLN("Failed to send temperature");
  } else {
    LOGLN("Success sending temperature");
  }
}

void loop()
{
  long requestedTemperatureMillis = 0;
  if (foundTempSensor) {
    requestedTemperatureMillis = startAndRequestTemperatures();
  }

#ifdef MOISTURE_SENSOR_CONNECTED
  doMoisture();
#endif

if (foundTempSensor) {
  doTemperature(millis() - requestedTemperatureMillis);
}

  LOG("Sleeping iterations:");
  LOGLN(ITERATIONS_60_MIN_IN_8S_INTERVAL);
  nrf24.sleep();
#ifdef DEBUG
  Serial.flush();
  delay(10000);
#else
  while(sleepCount <= ITERATIONS_60_MIN_IN_8S_INTERVAL) {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    sleepCount++;
  }
#endif
  LOG("Waking up after SleepCount: ");
  LOGLN(sleepCount);
  sleepCount = 0;
  timesWokenUp++;
}