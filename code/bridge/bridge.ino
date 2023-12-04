/***************************************************************************************
  Seat Occupancy Bridge (ESP-Now Slave)
  
  Created:   18 Jul 2021
  Author:   Gregory A Sarafin
  Project:  BU Thesis Project

  Hardware
    - Microcontroller       ESP32-WROOM

  This is the sketch code for the desk occupancy bridge node. This sketch will create a 
  node that will continually listen for an ESP-Now message. Upon receiving one, it will 
  upload the message to an MQTT stream with the sender's node ID as the feed path.

  Please note that although this bridge node does receive several data metrics from the 
  desk occupancy nodes, it only forwards the occupancy status metric onto the MQTT 
  server. This is necessary due to limitations with my MQTT service and its inability 
  to parse JSON messages. All of the various metrics are received by this bridge though,
  so they could be theoretically sent via MQTT if a different provider was used.


  IMPORTANT NOTE - Please make sure to set the relevant settings in the 'creds.h' file 
                   otherwise this sketch will not function as intended.
 **************************************************************************************/

/********************************* Libraries ******************************************/
#include <esp_now.h>

#include <WiFi.h>
#include "WiFiClientSecure.h"

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include "creds.h"

/********************************** Structures ****************************************/
typedef struct desk_sensor_msg {
  uint32_t nodeID;
  bool occupStatus;
  int8_t temp;
  int16_t noise;
} desk_sensor_msg_t;

/********************************* Declarations ***************************************/
esp_now_peer_info_t master;
desk_sensor_msg_t* nodeData;

WiFiClientSecure client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

bool triggerMQTT = false;

/******************************* Main Code Body ***************************************/
// Initialise ESP-Now
void initESPNow() {
  if (esp_now_init() !=  ESP_OK) {
    ESP.restart();                                // If init fails, restart ESP
  }
}

// Initialise Wi-Fi
void initWiFi() {
  WiFi.mode(WIFI_AP_STA);                         // Set Wi-Fi to Dual Station/AP Mode

  WiFi.begin(WLAN_SSID, WLAN_PASS);               // Connect to Wi-Fi Network
  delay(3000);

  while (WiFi.status() != WL_CONNECTED) {         // Wait for Successful Wi-Fi Connection
    delay(5000);
  }
}

// Connect to MQTT Service - Modified from Adafruit MQTT Library Function
void MQTT_connect() {
  int8_t ret;

  if (mqtt.connected()) {
    return;
  }

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) {      // Connect Will Return 0 For Connected
    mqtt.disconnect();
    delay(5000);
    retries--;
    if (retries == 0) {
      ESP.restart();                         // Three Connection Failures Will Restart ESP
    }
  }
}

// Called When a Node Sends Sensor Measurments
void dataRecvd(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  nodeData = (desk_sensor_msg_t*) data;
  triggerMQTT = true;
}

// Prepare and Send Received Data to MQTT
void sendMQTTMessage() {
  // Creates Feed Path Based on Sending Node's ID Number
  String tempFeed = AIO_USERNAME "/feeds/" + (String) nodeData->nodeID;
  char feedDir[tempFeed.length()];
  strcpy(feedDir, tempFeed.c_str());

  MQTT_connect();

  // Published to Feed
  Adafruit_MQTT_Publish(&mqtt, feedDir).publish(nodeData->occupStatus);

  /*
  // For Debugging
  Serial.println("");
  Serial.printf("Information about Last Packet Recv'd\n");
  Serial.printf("------------------------------------\n");
  Serial.print("Node ID:                "); Serial.println(nodeData->nodeID);
  Serial.print("Occupancy Status:       "); Serial.println(nodeData->occupStatus);
  Serial.print("Ambient Temperature:    "); Serial.println(nodeData->temp);
  Serial.print("Noise Level:            "); Serial.println(nodeData->noise);
  Serial.print("Data sent to MQTT feed: "); Serial.println(tempFeed);
  */
}

void setup() {
  // Serial.begin(115200);

  initWiFi();
  initESPNow();

  esp_now_register_recv_cb(dataRecvd);

  // Set Adafruit IO's root CA
  client.setCACert(adafruitio_root_ca);
}

void loop() {
  if (triggerMQTT) {
    sendMQTTMessage();
    nodeData = NULL;
    triggerMQTT = false;
  }
}