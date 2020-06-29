#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <DHTesp.h>
#include <numeric>
#include <vector>
#include "CanAirIoApi.hpp"
#include "MyConfigs.cpp"

/******************************************************************************
*   CONFIGS
******************************************************************************/

boolean DEBUG = true;

const int HPMA_PIN_RX = 12;
const int HPMA_PIN_TX = 13;
const int DHT_PIN = 14;

const int hpmaCheckInterval = 5000;
const int sendCheckInterval = 60000;

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

CanAirIoApi api(DEBUG);
SoftwareSerial hpmaSerial(HPMA_PIN_RX, HPMA_PIN_TX);
DHTesp dhtSensor;

/******************************************************************************
*   SETUP
******************************************************************************/

void setup() {
  Serial.begin(115200); Serial.println();
  hpmaSerial.begin(9600); hpmaSerial.println();
  
  delay(1000);

  if (DEBUG) Serial.println("Starting Setup");

  wifiInit();
  apiInit();
  sensorsInit();

}

void wifiInit() {
  if (DEBUG) Serial.println("Connecting to " + String(cfgWifiSsid));
  
  WiFi.begin(cfgWifiSsid, cfgWifiPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (DEBUG) Serial.println(".");
  }

  if (DEBUG) Serial.println("OK! IP: " + String(WiFi.localIP().toString()));
}

void apiInit() {
  if (DEBUG) Serial.println("Connecting to CanAirIoAPI");
  
  api.configure(cfgDeviceName, cfgDeviceId); 
  api.authorize(cfgApiUser, cfgApiPass);
  delay(1000);
  
  if (DEBUG) Serial.println("OK!");
}

void sensorsInit() {
  if (DEBUG) Serial.println("Initializing HPMA and DHT");

  stopHpmaAutosend();
  startHpmaMeasurement();
  dhtSensor.setup(DHT_PIN, DHTesp::DHT11);
  delay(3000);

  if (DEBUG) Serial.println("OK!");
}

/******************************************************************************
*   MAIN LOOP
******************************************************************************/

void loop() {
  currentMillis = millis();

  if ((unsigned long) currentMillis - lastHpmaMillis >= hpmaCheckInterval) {
    readPM();
    lastHpmaMillis = currentMillis;
  }

  if ((unsigned long) currentMillis - lastSendMillis >= sendCheckInterval) {
    if (calculatePmAverage()) {
      readHT();
      checkWifi();
      sendToApi();
    }
    lastSendMillis = currentMillis;
  }
}

void readPM() {
  unsigned int pm25 = 0;
  unsigned int pm10 = 0;

  if (readHpmaSensor(&pm25, &pm10)) {
    vectorInstant25.push_back(pm25);
    vectorInstant10.push_back(pm10);
    if (DEBUG) Serial.println("PM25=" + String(pm25) + " PM10=" + String(pm10));
  } else {
    Serial.println("Measurement fail");
  } 
}

boolean calculatePmAverage() {
  if (vectorInstant25.size() > 0) {
    averagePm25 = accumulate(vectorInstant25.begin(), vectorInstant25.end(), 0.0)/vectorInstant25.size();
    vectorInstant25.clear();
    averagePm10 = accumulate(vectorInstant10.begin(), vectorInstant10.end(), 0.0)/vectorInstant10.size();
    vectorInstant10.clear();
    if (DEBUG) Serial.println("Averages: PM25=" + String(averagePm25) + ", PM10=" + String(averagePm10));
    return true;
  } else {
    if (DEBUG) Serial.println("One of the vectors has 0 read values. Skipping cycle");
    return false;
  }
}

void readHT() {
  instantHumidity = dhtSensor.getHumidity();
  instantTemperature = dhtSensor.getTemperature();
  if (DEBUG) Serial.println("Temperature=" + String(instantTemperature) + " Humidity=" + String(instantHumidity));
}

void checkWifi() {
  if (!WiFi.isConnected()) {
    if (DEBUG) Serial.println("WiFi disconnected!");
    wifiInit();
    apiInit();
  }
}

void sendToApi() {
  if (DEBUG) Serial.println("API writing to " + String(api.url));

  bool status = api.write(averagePm1,averagePm25,averagePm10,instantHumidity,instantTemperature,cfgLatitude,cfgLongitude,cfgAltitude,cfgSpeed,sendCheckInterval / 1000);
  int code = api.getResponse();

  if (status) {
    if (DEBUG) Serial.println("OK! " + String(code));
  } else {
    if (DEBUG) Serial.println("FAIL! " + String(code));
  }
}


/******************************************************************************
*   HPMA Methods (From electronza's library)
******************************************************************************/

bool stopHpmaAutosend() {
  byte stop_autosend[] = {0x68, 0x01, 0x20, 0x77 };
  hpmaSerial.write(stop_autosend, sizeof(stop_autosend));

  while (hpmaSerial.available() < 2);
  byte read1 = hpmaSerial.read();
  byte read2 = hpmaSerial.read();
  
  if ((read1 == 0xA5) && (read2 == 0xA5)){
    return true; //ACK
  } 
  return false;
}

bool startHpmaMeasurement(){
  byte start_measurement[] = {0x68, 0x01, 0x01, 0x96 };
  hpmaSerial.write(start_measurement, sizeof(start_measurement));
  
  while (hpmaSerial.available() < 2);
  byte read1 = hpmaSerial.read();
  byte read2 = hpmaSerial.read();
  
  if ((read1 == 0xA5) && (read2 == 0xA5)){
    return true; //ACK
  }
  return false;
}

bool readHpmaSensor(unsigned int *pm25,unsigned int *pm10) {
  byte read_particle[] = {0x68, 0x01, 0x04, 0x93 };
  hpmaSerial.write(read_particle, sizeof(read_particle));

  while (hpmaSerial.available() < 1);
  byte HEAD = hpmaSerial.read();
  
  while (hpmaSerial.available() < 1);
  byte LEN = hpmaSerial.read();

  if ((HEAD == 0x96) && (LEN == 0x96)){
    if (DEBUG) Serial.println("NACK");
    return false; //NACK
  } else if ((HEAD == 0x40) && (LEN == 0x05)){
    //Valid Measurement
    while (hpmaSerial.available() < 1);
    byte COMD = hpmaSerial.read();
    while (hpmaSerial.available() < 1);
    byte DF1 = hpmaSerial.read(); 
    while (hpmaSerial.available() < 1);
    byte DF2 = hpmaSerial.read();     
    while (hpmaSerial.available() < 1);
    byte DF3 = hpmaSerial.read();   
    while (hpmaSerial.available() < 1);
    byte DF4 = hpmaSerial.read();     
    while (hpmaSerial.available() < 1);
    byte CS = hpmaSerial.read();      
    
    //Verify Checksum
    int checksum = (65536 - (HEAD + LEN + COMD + DF1 + DF2 + DF3 + DF4)) % 256;

    if (checksum != CS){
      if (DEBUG) Serial.println(String(checksum) + "!=" + String(CS));
      return false;
    } else {
      *pm25 = DF1 * 256 + DF2;
      *pm10 = DF3 * 256 + DF4;
      return true;
    }
  }
}
