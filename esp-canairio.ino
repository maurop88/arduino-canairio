#include <hpma115s0.h>
#include <DHTesp.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <numeric>
#include <vector>
#include "CanAirIoApi.hpp"

/******************************************************************************
*   CONFIGS
******************************************************************************/

boolean debug = true;

const int HPMA_PIN_RX = 14;
const int HPMA_PIN_TX = 12;
const int DHT_PIN = 2;

const int hpmaCheckInterval = 5000;
const int sendCheckInterval = 60000;

//Configurations for my device/wifi/account. Change them for yours.
const char cfgWifiSsid[] = "MY_SSID";
const char cfgWifiPass[] = "MY_PASS";
const long cfgLatitude = 0.000000;
const long cfgLongitude = 0.000000;
const long cfgAltitude = 0;
const long cfgSpeed = 0;
const char cfgDeviceName[] = "MY_NAME";
const char cfgDeviceId[] = "MY_ID";
const char cfgApiUser[] = "MY_USER";
const char cfgApiPass[] = "MY_PASS";

unsigned long currentMillis = 0;
unsigned long lastHpmaMillis = 0;
unsigned long lastSendMillis = 0;

std::vector<unsigned int> vectorInstant25;
std::vector<unsigned int> vectorInstant10;
unsigned int averagePm25 = 0;
unsigned int averagePm10 = 0;
unsigned int averagePm1 = 0;
unsigned int instantHumidity = 0;
unsigned int instantTemperature = 0;

CanAirIoApi api(debug);
SoftwareSerial softwareSerial(HPMA_PIN_RX, HPMA_PIN_TX);
HPMA115S0 hpmaSensor(softwareSerial);
DHTesp dhtSensor;

/******************************************************************************
*   SETUP
******************************************************************************/

void setup() {
  if (debug) Serial.println("Starting Setup");
  
  Serial.begin(9600);
  softwareSerial.begin(9600);
  
  wifiInit();
  apiInit();
  sensorsInit();
}

void wifiInit() {
  if (debug) Serial.println("Connecting to " + String(cfgWifiSsid));
  
  WiFi.begin(cfgWifiSsid, cfgWifiPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (debug) Serial.println(".");
  }

  if (debug) Serial.println("OK! IP: " + String(WiFi.localIP().toString()));
}

void apiInit() {
  if (debug) Serial.println("Connecting to CanAirIoAPI");
  
  api.configure(cfgDeviceName, cfgDeviceId); 
  api.authorize(cfgApiUser, cfgApiPass);
  delay(1000);
  
  if (debug) Serial.println("OK!");
}

void sensorsInit() {
  if (debug) Serial.println("Initializing HPMA and DHT");

  hpmaSensor.stop_autosend();
  hpmaSensor.start_measurement();

  pinMode(DHT_PIN, INPUT);
  dhtSensor.setup(DHT_PIN);

  if (debug) Serial.println("OK!");
}

/******************************************************************************
*   MAIN LOOP
******************************************************************************/

void loop() {
  currentMillis = millis();

  if ((unsigned long) currentMillis - lastHpmaMillis >= hpmaCheckInterval) {
    readHpmaSensor();
    lastHpmaMillis = currentMillis;
  }

  if ((unsigned long) currentMillis - lastSendMillis >= sendCheckInterval) {
    if (calculateHpmaAverage()) {
      readDhtSensor();
      checkWifi();
      sendToApi();
    }
    lastSendMillis = currentMillis;
  }
}

void readHpmaSensor() {
  float p25;
  float p10;

  if (hpmaSensor.read(&p25,&p10) == 1) {
    vectorInstant25.push_back((int)p25);
    vectorInstant10.push_back((int)p10);
    if (debug) Serial.println("PM25=" + String(p25) + " PM10=" + String(p10));
  } else {
    Serial.println("Measurement fail");
  } 
}

boolean calculateHpmaAverage() {
  if (vectorInstant25.size() > 0) {
    averagePm25 = accumulate(vectorInstant25.begin(), vectorInstant25.end(), 0.0)/vectorInstant25.size();
    vectorInstant25.clear();
    averagePm10 = accumulate(vectorInstant10.begin(), vectorInstant10.end(), 0.0)/vectorInstant10.size();
    vectorInstant10.clear();
    if (debug) Serial.println("Averages: PM25=" + String(averagePm25) + ", PM10=" + String(averagePm10));
    return true;
  } else {
    Serial.println("Error: one of the vectors has 0 read values. Skipping cycle");
    return false;
  }
}

void readDhtSensor() {
  instantTemperature = dhtSensor.getTemperature();
  instantHumidity = dhtSensor.getHumidity();
  if (debug) Serial.println("Temperature=" + String(instantTemperature) + " Humidity=" + String(instantHumidity));
}

void checkWifi() {
  if (!WiFi.isConnected()) {
    if (debug) Serial.println("WiFi disconnected!");
    wifiInit();
    apiInit();
  }
}

void sendToApi() {
  if (debug) Serial.println("API writing to " + String(api.url));

  bool status = api.write(averagePm1,averagePm25,averagePm10,instantHumidity,instantTemperature,cfgLatitude,cfgLongitude,cfgAltitude,cfgSpeed,sendCheckInterval / 1000);
  int code = api.getResponse();

  if (status) {
    if (debug) Serial.println("OK! " + String(code));
  } else {
    if (debug) Serial.println("FAIL! " + String(code));
  }
}
