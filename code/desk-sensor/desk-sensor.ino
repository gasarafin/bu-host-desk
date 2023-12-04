/***************************************************************************************
  Seat Occupancy Sensor (ESP-Now Master)

  Created:   12 Jul 2021
  Author:   Gregory A Sarafin
  Project:  BU Thesis Project

  Hardware
    - Microcontroller       ESP32-WROOM
    - Thermopile Sensor     MLX90614
    - Loudness Sensor       KY-037 (Optional)
  
  This is the sketch code for the desk occupancy sensor node. This sketch will start by 
  creating a node ID that is uniquely set via hardware attributes to ensure each node has 
  a unique ID set automatically. It will use the onboard thermal sensor to measure the 
  current room temperature, calculate if the seat is occupied, sample the level of human 
  perceived loudness at the area of the sensor, and then package all of that information 
  into a custom struct to send via the ESP-Now protocol. The system will then sleep for a 
  predetermined amount of time (as set in the creds.h file) before repeating the process. 
  ESP specific functions are used in this sketch to enable deep sleep and variable 
  retention functionality.


  IMPORTANT NOTE - Please make sure to set the relevant settings in the 'creds.h' file 
                   otherwise default values will be used.

 **************************************************************************************/

/********************************* Libraries ******************************************/
#include <esp_now.h>
#include <WiFi.h>

#include <Adafruit_MLX90614.h>

#include "creds.h"

/********************************** Definitions ***************************************/
#define ANALOG_NOISE_INPUT_GPIO 2
#define NOISE_SENSOR_BASELINE_THRESHOLD 0

/********************************** Structures ****************************************/
typedef struct desk_sensor_msg {
  uint32_t nodeID;
  bool occupStatus;
  int8_t temp;
  int16_t noise;
} desk_sensor_msg_t;

/********************************* Declarations ***************************************/
// Anything that starts with 'RTC_DATA_ATTR' persists between deep sleeps in the
// real-time clock memory since core memory is turned off in deep sleep, but RTC memory
// is not. 
RTC_DATA_ATTR esp_now_peer_info_t slave;
RTC_DATA_ATTR bool firstRun = true;
RTC_DATA_ATTR bool emptyTimeout = false;

RTC_DATA_ATTR static uint32_t myNodeID = 0;

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
RTC_DATA_ATTR static bool lastSensorState = false;

static const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const uint16_t noiseBaseline = NOISE_SENSOR_BASELINE_THRESHOLD;

/******************************* Main Code Body ***************************************/
// Initialise ESP-Now
void initESPNow() {
  if (esp_now_init() !=  ESP_OK) {
    ESP.restart();                                // If init fails, restart ESP
  }
}

// Set unique node ID based on MAC
void setNodeID() {
  byte myMac[6];
  WiFi.macAddress(myMac);

  myNodeID |= (uint8_t)myMac[2] << 24;
  myNodeID |= (uint8_t)myMac[3] << 16;
  myNodeID |= (uint8_t)myMac[4] << 8;
  myNodeID |= (uint8_t)myMac[5];
}

// Measure & send sensor data
void sendData() {
  const uint8_t *peer_addr = slave.peer_addr;
  desk_sensor_msg_t deskData;
  double objTemp = mlx.readObjectTempC();
  double ambTemp = mlx.readAmbientTempC();

  // Node ID
  deskData.nodeID = myNodeID;

  // Occupancy Status
  // Occupied if differential between object and ambient temperature is greater than 1
  deskData.occupStatus = (objTemp-ambTemp)>1;

  // If the desk changed from occupied to non-occupied there is a timeout before
  // reporting the desk as available to allow for short work breaks.
  if (deskData.occupStatus != lastSensorState) {
    if (deskData.occupStatus == 0) {
      //esp_sleep_enable_ext0_wakeup(GPIO_NUM_5,1); //For use with TPiS Calipile
      emptyTimeout = true;
      esp_sleep_enable_timer_wakeup(TIMEOUT_BEFORE_SEAT_FREE_IN_SECONDS * 1000000);
      esp_deep_sleep_start();
      // This is essentially a program break
    }
  }
  lastSensorState = deskData.occupStatus;

  // Room Temperature 
  deskData.temp = mlx.readAmbientTempC();

  // Location Noise Level
  deskData.noise = analogRead(ANALOG_NOISE_INPUT_GPIO) - noiseBaseline; 
  
  // Send Data
  esp_now_send(peer_addr, (uint8_t *) &deskData, sizeof(deskData));
}

// Stub callback function
void dataSentCB(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_FAIL) {
    // Packet Sent
  } else {
    // Packet Not Sent
  }
}

void setup() {
  WiFi.mode(WIFI_STA);
  initESPNow();
  esp_now_add_peer(&slave);
  esp_now_register_send_cb(dataSentCB);

  mlx.begin();
  delay(300);

  // Only called on initial startup to set values store in RTC memory
  if (firstRun) {
    memcpy(slave.peer_addr, broadcastAddress, sizeof(broadcastAddress));
    slave.channel = WIRELESS_CHANNEL;
    slave.encrypt = 0;
    setNodeID();
    firstRun = false;
  }
  
  // This only runs if the seat is becoming free or temporarily vacant.
  if (emptyTimeout) {
    lastSensorState = (mlx.readObjectTempC()-mlx.readAmbientTempC())>1;
    emptyTimeout = false;
  }
}

void loop() {
  sendData();
  delay(100);

  // Send ESP to sleep for predetermined period (sent in 'creds.h')
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_IN_SECONDS * 1000000);
  esp_deep_sleep_start();
  // This is essentially a program break
}