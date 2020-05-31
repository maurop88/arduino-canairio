#include <hpma115s0.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <numeric>
#include <vector>
#include "CanAirIoApi.hpp"

/******************************************************************************
*   CONFIGS
******************************************************************************/

boolean debug = true;
long cfgLatitude = 0;
long cfgLongitude = 0;
long cfgAltitude = 0;
long cfgSpeed = 0;
int cfgHpmaReadDelay = 5;
int cfgSendDelay = 5;
char cfgWifiSsid[] = "MY_SSID";
char cfgWifiPass[] = "MY_PASS";
char cfgDeviceName[] = "MY_NAME";
char cfgDeviceId[] = "MY_ID";
char cfgApiUser[] = "MY_USER";
char cfgApiPass[] = "MY_PASS";

std::vector<unsigned int> vectorInstant25;
std::vector<unsigned int> vectorInstant10;
unsigned int averagePm25 = 0;
unsigned int averagePm10 = 0;
unsigned int averagePm1 = 0;
unsigned int instantHumidity = 0;
unsigned int instantTemperature = 0;

unsigned int HPMA_PIN_RX = 14;
unsigned int HPMA_PIN_TX = 12;

CanAirIoApi api(debug);
SoftwareSerial softwareSerial(HPMA_PIN_RX, HPMA_PIN_TX);
HPMA115S0 hpma(softwareSerial);

/******************************************************************************
*   SETUP
******************************************************************************/

void setup() {
  
  Serial.begin(9600);
  softwareSerial.begin(9600);
  
  if (debug) Serial.println("Starting Setup");
  
  wifiInit();
  apiInit();
  sensorInit();

}

void wifiInit() {
  if (debug) Serial.println("Connecting to " + String(cfgWifiSsid));
  
  WiFi.begin(cfgWifiSsid, cfgWifiPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    if (debug) Serial.println(".");
  }

  if (debug) Serial.println("OK! IP: " + String(WiFi.localIP().toString()));
}

void apiInit() {
  if (debug) Serial.println("Connecting to CanAirIoAPI... ");
  
  api.configure(cfgDeviceName, cfgDeviceId); 
  api.authorize(cfgApiUser, cfgApiPass);
  delay(1000);
  
  if (debug) Serial.println("OK!");
}

void sensorInit() {
  if (debug) Serial.println("Initializing sensor... ");
  
  hpma.stop_autosend();
  hpma.start_measurement();
  
  if (debug) Serial.println("OK!");
}

/******************************************************************************
*   MAIN LOOP
******************************************************************************/

void loop() {
  sensorLoop();
  averageLoop();
  wifiLoop();
  apiLoop();
  delay(cfgHpmaReadDelay * 1000);
}

void sensorLoop() {
  float p25;
  float p10;

  if (hpma.read(&p25,&p10) == 1) {
    vectorInstant25.push_back((int)p25);
    vectorInstant10.push_back((int)p10);
    if (debug) Serial.println("PM25=" + String(p25) + " PM10=" + String(p10));
  } else {
    if (debug) Serial.println("Measurement fail");
  } 
}

void averageLoop() {
  if (vectorInstant25.size() >= cfgSendDelay){
    averagePm25 = accumulate(vectorInstant25.begin(), vectorInstant25.end(), 0.0)/vectorInstant25.size();
    vectorInstant25.clear();
    averagePm10 = accumulate(vectorInstant10.begin(), vectorInstant10.end(), 0.0)/vectorInstant10.size();
    vectorInstant10.clear();
    if (debug) Serial.println("average PM25=" + String(averagePm25) + " average PM10=" + String(averagePm10));
  } 
}

void wifiLoop() {
  if (vectorInstant25.size() == 0 && !WiFi.isConnected()) {
    if (debug) Serial.println("WiFi disconnected!");
    wifiInit();
    apiInit();
  }
}

void apiLoop() {
  if (vectorInstant25.size() == 0) {
    if (debug) Serial.println("API writing to " + String(api.url));

    bool status = api.write(averagePm1,averagePm25,averagePm10,instantHumidity,instantTemperature,cfgLatitude,cfgLongitude,cfgAltitude,cfgSpeed,cfgHpmaReadDelay);
    int code = api.getResponse();
    if (status) {
      if (debug) Serial.println("OK! " + String(code));
    } else {
      if (debug) Serial.println("FAIL! " + String(code));
    }
  }
}
