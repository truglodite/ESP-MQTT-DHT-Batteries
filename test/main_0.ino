// main.ino
// EspMqttDHTbatts
// ESP8266 + DHTXX + deepsleep + battery monitor + OTA
// 6/21/2019
// by: Truglodite
//
// Connects to mqtt broker, pubs "temp" & "humid" @qos 0, and subs to
// "ota" @qos 1. User configs are in configuration.h. Comment out the
// line below, or rename your configuration.h to privacy.h.
/////////////////////////////////////////////////////////////////////////////
#define privacy

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <DHT.h>
extern "C" {
  #include "user_interface.h"
}
#include "configuration.h"
#ifdef privacy
  #include "privacy.h"
#endif

bool firmwareUp = 0;             // OTA flag
bool isFirmwareUpSet = 0;        // OTA sync flag
bool OTAnotificationSent = 0;    // OTA notification flag
bool batteryLow = 0;             // Low battery flag

unsigned long dhtStartTime = 0;  // DHT timeout timer
unsigned long otaStartTime = 0;  // OTA timeout timer
unsigned long connectStartTime = 0; // connection timeout timer

float t = 0.0;                   // temperature
float h = 0.0;                   // humidity
#ifdef batteryMonitor
  float vbatt = 0.0;             // battery voltage
#endif

int retries = 0;                 // DHT read retry count

// stringy mess of char nulls
char tempTopic[sizeof(mqttClientID) + 1 + sizeof(tempTopicD) + 1] = {0};
char humidTopic[sizeof(mqttClientID) + 1 + sizeof(humidTopicD) + 1] = {0};
char otaTopic[sizeof(mqttClientID) + 1 + sizeof(otaTopicD) + 1] = {0};
char notifyOTAready[sizeof(notifyOTAreadyB) + 45 + sizeof(update_path) + 1] = {0};
char battTopic[sizeof(mqttClientID) + 1 + sizeof(battTopicD) + 1] = {0};

IPAddress myIP;

BearSSL::ESP8266WebServerSecure httpServer(443);  // TLS web server init
ESP8266HTTPUpdateServer httpUpdater;              // Browser OTA server init
WiFiClient espClient;                             // Wifi init

DHT dht(dhtPin,dhtType);                          // DHT init

// Mosquitto callback (subs) ////////////////////////////////////////////////
void callback(char* topic, byte* payload, unsigned int length) {
  #ifdef debug
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (u_int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  #endif

  // "otaTopic: 1" ("1" is the first payload character)
  if(!strcmp(topic, otaTopic) && (char)payload[0] == '1') {
    #ifdef debug
      Serial.println("Turning on OTA!");
    #endif
    firmwareUp = 1;
    firmwareUpSet = 1;
  }

  // "otaTopic: 0" (0 is first payload character)
  else if(!strcmp(topic, otaTopic) && (char)payload[0] == '0') {
    #ifdef debug
      Serial.println("Turning off OTA!");
    #endif
    firmwareUp = 0;
    firmwareUpSet = 1;
  }
  return;
}
PubSubClient client(brokerIP, brokerPort, callback, espClient);

// SETUP ////////////////////////////////////////////////////////////////////
void setup() {
  pinMode(dhtPowerPin,OUTPUT);             // Setup sensor power switch
  digitalWrite(dhtPowerPin,LOW);           // Make sure DHT is off
  #ifdef debug
    Serial.begin(115200);
    Serial.println("Debug Enabled...");
  #endif
  WiFi.mode(WIFI_OFF);                     // wifi off for a clean adc read
  WiFi.forceSleepBegin();
  delay(1);

  #ifdef batteryMonitor
    delay(100);                            // Help with analog read stability?
    #ifdef debug
      Serial.print("Reading battery: ");
    #endif
    vbatt = analogRead(A0);
    vbatt = vbatt * vbattRatio / 1023.0;   // 10bit battery voltage calc
    #ifdef testBoard
      vbatt = 3.7;
    #endif
    #ifdef debug
      Serial.println(vbatt);
    #endif

    if(vbatt < vbattCrit) { // vbatt critical, shutdown immediately
      #ifdef debug
        Serial.println("Critical Battery Shut Down...");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(0,WAKE_RF_DISABLED);
      yield();
    }
    else if(vbatt <= vbattLow) {
      #ifdef debug
        Serial.println("Low battery flag set");
      #endif
      batteryLow = 1;                      // set low battery flag
    }
  #endif

  digitalWrite(dhtPowerPin,HIGH);          // Turn on sensor power
  // Form our topic strings
  sprintf(otaTopic, "%s/%s", mqttClientID, otaTopicD);
  sprintf(tempTopic, "%s/%s", mqttClientID, tempTopicD);
  sprintf(humidTopic, "%s/%s", mqttClientID, humidTopicD);
  sprintf(battTopic, "%s/%s", mqttClientID, battTopicD);

  delay(dhtDelay);                         // sensor warmup time
  #ifdef debug
    Serial.println("DHT warmup time...");
  #endif
  dht.begin();
  dhtStartTime = millis();
  while(millis() - dhtStartTime < dhtTimeout);// Wait before reading
  h = dht.readHumidity();                  // RH %
  t = dht.readTemperature(true);           // Temp Farenheit True
  #ifdef testBoard
    h = 23.4;
    t = 78.9;
  #endif

  while(isnan(h) || isnan(t)) {            // Retry reading(s) for any NAN's
    #ifdef debug
      Serial.println("DHT nan retry");
    #endif
    delay(dhtTimeout);
    if(isnan(h)) {h = dht.readHumidity();};
    if(isnan(t)) {t = dht.readTemperature(true);};
    retries ++;
    if(retries > retriesMax)  {            // too many nans, reboot
      #ifdef debug
        Serial.println(notifyDHTfail);
      #endif
      digitalWrite(dhtPowerPin,LOW);       // This could 'quiet bootloop'!!!
      ESP.deepSleep(60000,WAKE_RF_DISABLED);// risk this to avoid wifi mucking the DHT measurements :/
      delay(500);
      yield();
    }
  }

  digitalWrite(dhtPowerPin,LOW);       // Done reading, turn sensor off

  WiFi.forceSleepWake();               // Turn wifi on
  WiFi.mode(WIFI_STA);
  wifi_station_set_hostname(hostName);
  #ifdef customMac
    wifi_set_macaddr(STATION_IF, mac);
  #endif
  WiFi.begin(ssid, pass);
  #ifdef debug
    Serial.print("Connecting wifi");
  #endif
  connectStartTime = millis();
  while(WiFi.status() != WL_CONNECTED) {
    if(millis() - connectStartTime > connectTimeout) {
      #ifdef debug
        Serial.println("Wifi timeout");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(5000000,WAKE_RF_DISABLED); // deepsleep 5 sec
      delay(500);
    }
    delay(500);
    #ifdef debug
      Serial.print(".");
    #endif
  }

  myIP = WiFi.localIP();
  sprintf(notifyOTAready, "%s%s%s", notifyOTAreadyB, myIP.toString().c_str(), update_path); // make the ota payload

  #ifdef debug
    Serial.println();
    Serial.print("IP Address: ");
    Serial.println(myIP);
    Serial.print("Connecting broker: ");
  #endif

  client.setServer(brokerIP, brokerPort);
  client.setCallback(callback);
  connectStartTime = millis();
  client.connect(mqttClientID, mqttClientUser, mqttClientPass);
  while (!client.connected()) {
    if(millis() - connectStartTime > connectTimeout) {
      #ifdef debug
        Serial.println("Broker timeout");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(5000000,WAKE_RF_DISABLED); // deepsleep 5 sec
      delay(500);
    }
    yield();
  }
  client.subscribe(otaTopic, 1);

  #ifdef debug
    Serial.print(brokerIP);
    Serial.print(":");
    Serial.println(brokerPort);
    Serial.println("Subs: ");
    Serial.print("    ");
    Serial.println(otaTopic);
  #endif

  //Initialize OTA server
  #ifdef debug
    Serial.println("Initializing OTA");
  #endif
  configTime(8 * 3600, 0, ntpServer1, ntpServer2);
  MDNS.begin(hostName);
  httpServer.setRSACert(new BearSSL::X509List(serverCert), new BearSSL::PrivateKey(serverKey));
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("https", "tcp", 443);
}

// LOOP /////////////////////////////////////////////////////////////////////
void loop(){
  client.loop();  // Process PubSubClient

  // OTA off: normal routine... upload data, deepsleep
  if(!firmwareUp && isFirmwareUpSet){
    uploadData();
  }
  // OTA on, battery ok, message not sent: send message, set flag, start timer
  else if(firmwareUp && !OTAnotificationSent && !batteryLow){
    #ifdef debug
      Serial.println("Sending OTA ready notification");
    #endif
    if(!client.publish(otaTopic, notifyOTAready))  {
      #ifdef debug
        Serial.println("OTA notify failed");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
      yield();
    }
    OTAnotificationSent = 1;
    otaStartTime = millis();
  }

  // OTA on, battery low, message not sent: send different message, cancel ota
  else if(firmwareUp && !OTAnotificationSent && batteryLow) {
    #ifdef debug
      Serial.print("Sending: [");
      Serial.print(otaTopic);
      Serial.print("]: ");
      Serial.println(notifyOTAlowbatt);
    #endif
    if(!client.publish(otaTopic, notifyOTAlowbatt))  {
      #ifdef debug
        Serial.println("OTA_lowbatt notify failed");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
      yield();
    }
    OTAnotificationSent = 1;
    firmwareUp = 0;
  }

  // FW button on & battery OK: handle OTA calls
  else if(firmwareUp && !batteryLow) {
    httpServer.handleClient();
    MDNS.update();
    if(millis() - otaStartTime > otaTimeout)  {// OTA timeout... sleep 5sec
      #ifdef debug
        Serial.print("Sending: [");
        Serial.print(otaTopic);
        Serial.print("]: ");
        Serial.println(notifyOTAtimeout);
      #endif
      if(!client.publish(otaTopic, notifyOTAtimeout))  {
        #ifdef debug
          Serial.println("OTA_timeout notify failed");
        #endif
      }
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(5000000,WAKE_RF_DISABLED); // deepsleep 5 sec
      delay(500);
    }
  }
}
//**********************  END LOOP  *************************//
///////////////////////////////////////////////////////////////

// The main routine that runs after the Vpins are downloaded
void uploadData()  {
  // Write data to virtual pins
  char humidStr[7] = {0};  // declare strings
  char tempStr[7] = {0};
  dtostrf(h, 4, 1, humidStr); // convert: 6 wide, precision 1 (-999.9)
  dtostrf(t, 4, 1, tempStr);
  #ifdef debug
    Serial.println("Publishing data:");
  #endif

  // publish
  if(!client.publish(humidTopic, humidStr))  {
    #ifdef debug
      Serial.println("humid pub failed");
    #endif
    digitalWrite(dhtPowerPin,LOW);
    ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
    delay(500);
  }
  #ifdef debug
    Serial.print("    ");
    Serial.print(humidTopic);
    Serial.print(": ");
    Serial.println(humidStr);
  #endif
  if(!client.publish(tempTopic, tempStr))  {
    #ifdef debug
      Serial.println("temp pub failed");
    #endif
    digitalWrite(dhtPowerPin,LOW);
    ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
    delay(500);
  }
  yield();
  #ifdef debug
    Serial.print("    ");
    Serial.print(tempTopic);
    Serial.print(": ");
    Serial.println(tempStr);
  #endif

  #ifdef extraOutput
    float rssi =  WiFi.RSSI();              // RSSI in dBm
    char rssiStr[7] = {0};
    char rssiTopic[sizeof(rssiStr) + 2 + sizeof(mqttClientID)] = {0};
    dtostrf(rssi, 4, 1, rssiStr); // convert: 6 wide, precision 1 (-999.9)
    sprintf(rssiTopic, "%s/rssi", mqttClientID);
    #ifdef debug
      Serial.print("    ");
      Serial.print(rssiTopic);
      Serial.print(": ");
      Serial.println(rssiStr);
    #endif
    if(!client.publish(rssiTopic, rssiStr))  {
      #ifdef debug
        Serial.println("rssi pub failed");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
    }
    yield();
  #endif

  #ifdef batteryMonitor
    char battStr[8] = {0};
    dtostrf(vbatt, 4, 2, battStr);
    if(!client.publish(battTopic, battStr))  {
      #ifdef debug
        Serial.println("vbatt pub failed");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
      delay(500);
    }
    client.loop();  // Process PubSubClient
    delay(10);  // Process PubSubClient
    yield();
    #ifdef debug
      Serial.print("    ");
      Serial.print(battTopic);
      Serial.print(": ");
      Serial.println(battStr);
    #endif
  #endif

  #ifdef debug
    Serial.println("Done... entering deepsleep");
  #endif

  #ifdef testBoard
    digitalWrite(dhtPowerPin,LOW);
    #ifdef debug
      Serial.println("...test deepsleep");
    #endif
    ESP.restart();  // for testing
    delay(500);
  #endif

  if(!batteryLow && !firmwareUp) {
    digitalWrite(dhtPowerPin,LOW);
    ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);  // normal sleep
    delay(500);
  }
  else if(!firmwareUp){
    digitalWrite(dhtPowerPin,LOW);
    ESP.deepSleep(longSleepMicros,WAKE_RF_DISABLED); // low batt long sleep
    delay(500);
  }
  yield();
}
