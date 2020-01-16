// main.ino
// EspMqttDHTbatts
// ESP8266 + DHTXX + deepsleep + battery monitor + OTA
// 6/21/2019
// by: Truglodite
//
// Connects to mqtt broker, pubs "temp", "humid", & "batt" @qos 0.
// Subs to "ota", "temp", "humid", & "batt"  @qos 1.
// User configs are in configuration.h.
// Comment out the line below, or rename your configuration.h to privacy.h.
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
bool isBattSet = 0;
bool isHumidSet = 0;
bool isTempSet = 0;

unsigned long dhtStartTime = 0;  // DHT timeout timer
unsigned long otaStartTime = 0;  // OTA timeout timer
unsigned long connectStartTime = 0; // connection timeout timer
unsigned long subStartTime = 0; // connection timeout timer

float t = 0.0;                   // temperature
float tBroker = 0.0;
float h = 0.0;                   // humidity
float hBroker = 0.0;
#ifdef batteryMonitor
  float vbatt = 0.0;             // battery voltage
  float vbattBroker = 0.0;       // battery voltage from broker
#endif

int retries = 0;                 // DHT read retry count
int pubCount = 0;                // number of pub retries
int state = 0;                   // State machine variable

// stringy mess of char nulls
char humidStr[7] = {0};  // declare strings
char tempStr[7] = {0};
char tempTopic[sizeof(mqttClientID) + 1 + sizeof(tempTopicD) + 1] = {0};
char humidTopic[sizeof(mqttClientID) + 1 + sizeof(humidTopicD) + 1] = {0};
char otaTopic[sizeof(mqttClientID) + 1 + sizeof(otaTopicD) + 1] = {0};
char notifyOTAready[sizeof(notifyOTAreadyB) + 45 + sizeof(update_path) + 1] = {0};
#ifdef batteryMonitor
  char battTopic[sizeof(mqttClientID) + 1 + sizeof(battTopicD) + 1] = {0};
  char battStr[8] = {0};
#endif
#ifdef extraOutput
  float rssi =  0.0;              // RSSI in dBm
  char rssiStr[7] = {0};
  char rssiTopic[sizeof(rssiStr) + 2 + sizeof(mqttClientID)] = {0};
#endif

IPAddress myIP;

ESP8266WebServerSecure httpServer(443);  // TLS web server init
ESP8266HTTPUpdateServerSecure httpUpdater;              // Browser OTA server init
WiFiClient espClient;                             // Wifi init

DHT dht(dhtPin,dhtType);                          // DHT init

// Mosquitto callback (subs) ////////////////////////////////
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
    isFirmwareUpSet = 1;  // flag to ensure retained subs get synced
  }

  // "otaTopic: 0" (0 is first payload character)
  else if(!strcmp(topic, otaTopic) && (char)payload[0] == '0') {
    #ifdef debug
      Serial.println("Turning off OTA!");
    #endif
    firmwareUp = 0;
    isFirmwareUpSet = 1;
  }

  // "battTopic" (float X.XX)
  else if(!strcmp(topic, battTopic)) {
    #ifdef debug
      Serial.print("Converting batt: ");
    #endif
    char payloadBuffer[length];
    for(u_int i = 0; i < length; i++) {
      payloadBuffer[i] = (char)payload[i];
    }
    payloadBuffer[length] = '\0';
    String payloadMessage = String(payloadBuffer);

    vbattBroker = payloadMessage.toFloat();
    isBattSet = 1;
    #ifdef debug
      Serial.println(vbattBroker);
    #endif
  }

  // "humidTopic" (float XXX.X)
  else if(!strcmp(topic, humidTopic)) {
    #ifdef debug
      Serial.print("Converting humid: ");
    #endif
    char payloadBuffer[length];
    for(u_int i = 0; i < length; i++) {
      payloadBuffer[i] = (char)payload[i];
    }
    payloadBuffer[length] = '\0';
    String payloadMessage = String(payloadBuffer);

    hBroker = payloadMessage.toFloat();
    isHumidSet = 1;
    #ifdef debug
      Serial.println(hBroker);
    #endif
  }

  // "tempTopic" (float XXX.XX)
  else if(!strcmp(topic, tempTopic)) {
    #ifdef debug
      Serial.print("Converting temp: ");
    #endif
    char payloadBuffer[length];
    for(u_int i = 0; i < length; i++) {
      payloadBuffer[i] = (char)payload[i];
    }
    payloadBuffer[length] = '\0';
    String payloadMessage = String(payloadBuffer);

    tBroker = payloadMessage.toFloat();
    isTempSet = 1;
    #ifdef debug
      Serial.println(tBroker);
    #endif
  }

  return;
} // end callback

PubSubClient client(brokerIP, brokerPort, callback, espClient);

// SETUP ////////////////////////////////////////////////////
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
    int vbatti = vbatt * 100.0; // dirty trick to ensure precision matches strings later on
    vbatt = (float)vbatti / 100.0;
    #ifdef testBoard
      vbatt = testBoardVbatt;
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
  #ifdef extraOutput
    sprintf(rssiTopic, "%s/rssi", mqttClientID);
  #endif

  delay(dhtWarmup);                         // sensor warmup time
  #ifdef debug
    Serial.println("DHT warmup time...");
  #endif
  dht.begin();
  dhtStartTime = millis();
  while(millis() - dhtStartTime < dhtTimeout){}// Wait before reading

  h = dht.readHumidity();                  // RH %
  int hi = h * 10.0; // dirty trick to ensure precision matches strings later on
  h = (float)hi / 10.0;

  t = dht.readTemperature(true);           // Temp Farenheit True
  int ti = t * 10.0;
  t = (float)ti / 10.0;
  #ifdef testBoard
    h = testBoardH;
    t = testBoardT;
  #endif

  while(isnan(h) || isnan(t)) { // Retry reading(s) for any NAN's
    #ifdef debug
      Serial.println("DHT nan retry");
    #endif
    delay(dhtTimeout);
    if(isnan(h)) {h = dht.readHumidity();};
    if(isnan(t)) {t = dht.readTemperature(true);};
    retries ++;
    if(retries > retriesMax)  {  // too many nans, reboot
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
  client.subscribe(battTopic, 1);
  client.subscribe(humidTopic, 1);
  client.subscribe(tempTopic, 1);

  #ifdef debug
    Serial.print(brokerIP);
    Serial.print(":");
    Serial.println(brokerPort);
    Serial.println("Subs: ");
    Serial.print("    ");
    Serial.println(otaTopic);
    Serial.print("    ");
    Serial.println(battTopic);
    Serial.print("    ");
    Serial.println(humidTopic);
    Serial.print("    ");
    Serial.println(tempTopic);
  #endif

  //Initialize OTA server
  #ifdef debug
    Serial.println("Initializing OTA");
  #endif
  configTime(timeZone * 3600, 0, ntpServer1, ntpServer2);
  MDNS.begin(hostName);
  httpServer.getServer().setRSACert(new BearSSL::X509List(serverCert), new BearSSL::PrivateKey(serverKey));
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("https", "tcp", 443);

  // Create our data to strings for publishing
  dtostrf(h, 4, 1, humidStr); // convert: 6 wide, precision 1 (-999.9)
  dtostrf(t, 4, 1, tempStr);
  dtostrf(vbatt, 4, 2, battStr);
  #ifdef extraOutput
    rssi =  WiFi.RSSI();              // RSSI in dBm
    dtostrf(rssi, 4, 1, rssiStr); // convert: 6 wide, precision 1 (-999.9)
  #endif

  subStartTime = millis(); // start sub timer
}

// LOOP /////////////////////////////////////////////////////
void loop(){
  client.loop();  // Process PubSubClient

  // ota subscription timed out (use retain on the ota pub to prevent this)
  if(!isFirmwareUpSet && millis() - subStartTime > subTimeout)  {
    isFirmwareUpSet = 1; // continue without subscribed data
  }

  // ota = 0... upload stuff
  if(isFirmwareUpSet && !firmwareUp){
    publishData();
  }
  // OTA on, battery ok, message not sent: send message, set flag, start timer
  else if(firmwareUp && !OTAnotificationSent && !batteryLow){
    #ifdef debug
      Serial.print("Sending: [");
      Serial.print(otaTopic);
      Serial.print("]: ");
      Serial.println(notifyOTAready);
    #endif
    if(!client.publish(otaTopic, notifyOTAready))  {
      #ifdef debug
        Serial.println("ota notify failed");
      #endif
      digitalWrite(dhtPowerPin,LOW);
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);
      yield();
    }
    OTAnotificationSent = 1;
    otaStartTime = millis();
  }

  // ota = 1, low battery, message not sent: send different message, cancel ota
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
      ESP.deepSleep(5000000,WAKE_RF_DISABLED); // deepsleep 5 sec when ota ready
      delay(500);
    }
  }
}
// END LOOP /////////////////////////////////////////////////

// The main routine that runs after the Vpins are downloaded
void publishData()  {
switch(state) {
    // Init: after setup, broker connected & local data processed
    // 0 = publish data
    // 1 = return to loop until subs are filled: matching->2, mismatch->0
    // 2 = deepsleep

  case 0:{ // publish ---------------------------------------
    #ifdef debug
      Serial.println("Publishing data:");
    #endif
    // Humid
    if(!client.publish(humidTopic, humidStr, true))  {
      #ifdef debug
        Serial.println("humid pub failed");
      #endif
      ESP.restart();
    }
    #ifdef debug
      Serial.print("    ");
      Serial.print(humidTopic);
      Serial.print(": ");
      Serial.println(humidStr);
    #endif
    // Temp
    if(!client.publish(tempTopic, tempStr, true))  {
      #ifdef debug
        Serial.println("temp pub failed");
      #endif
      ESP.restart();
    }
    yield();
    #ifdef debug
      Serial.print("    ");
      Serial.print(tempTopic);
      Serial.print(": ");
      Serial.println(tempStr);
    #endif
    // RSSI
    #ifdef extraOutput
      #ifdef debug
        Serial.print("    ");
        Serial.print(rssiTopic);
        Serial.print(": ");
        Serial.println(rssiStr);
      #endif
      if(!client.publish(rssiTopic, rssiStr, true))  {
        #ifdef debug
          Serial.println("rssi pub failed");
        #endif
        ESP.restart();
      }
      yield();
    #endif
    // Battery
    #ifdef batteryMonitor
      if(!client.publish(battTopic, battStr, true))  {
        #ifdef debug
          Serial.println("vbatt pub failed");
        #endif
        ESP.restart();
      }
      yield();
      #ifdef debug
        Serial.print("    ");
        Serial.print(battTopic);
        Serial.print(": ");
        Serial.println(battStr);
      #endif
    #endif

    isHumidSet = 0; // set flags to verify with broker
    isTempSet = 0; // set flags to verify with broker
    isBattSet = 0;
    state = 1;
    pubCount++;
    subStartTime = millis();
    break;
  } // case 0

  case 1:{ // verify data -----------------------------------
    // just loop client until subs are ready
    if(isHumidSet && isTempSet && isBattSet)  {
      if(hBroker == h && tBroker == t && vbattBroker == vbatt) {  // all data are matching
        #ifdef debug
          Serial.println("data matches broker, continuing...");
        #endif
        pubCount = 0;  // not needed for this code, really
        state = 2;
      }
      else if(millis() - subStartTime > repubDelay) { // broker doesn't match, & it's time to republish
        if(pubCount > repubsMax)  {  // too many repubs
          #ifdef debug
            Serial.println("Err: too many repubs");
          #endif
          ESP.restart();
          yield();
        }
        #ifdef debug
          Serial.println("sub mismatch, republishing");
        #endif
        state = 0;
      }
    }
    // sub timeout
    if(millis() - subStartTime > subTimeout*1000) {
      #ifdef debug
        Serial.println("Second sub timeout");
      #endif
      ESP.restart();
    }
    yield();
    break;
  } // case 1

  case 2:{ // shutdown --------------------------------------
    #ifdef debug
      Serial.println("Done... entering deepsleep");
    #endif

    #ifdef testBoard
      unsigned long runTime = millis()/1000;
      #ifdef debug
        Serial.println("...test deepsleep");
        Serial.print("Runtime: ");
        Serial.print(runTime);
        Serial.println(" seconds");
      #endif
      ESP.restart();  // for testing
      delay(500);
    #endif
    // In case a late OTA shows up, let's do that, otherwise
    if(!batteryLow && !firmwareUp) {  // batt ok, normal sleeptime
      #ifdef debug
        Serial.println("Normal deepsleep...");
      #endif
      ESP.deepSleep(sleepMicros,WAKE_RF_DISABLED);  // normal sleep
    }
    else if(!firmwareUp){  // battery low, longer sleep
      #ifdef debug
        Serial.println("Low batt, long deepsleep...");
      #endif
      ESP.deepSleep(longSleepMicros,WAKE_RF_DISABLED); // low batt long sleep
      delay(500);
    }
    yield();
  break;
} // case 2

  default:{ // NA --------------------------------------
    #ifdef debug
      Serial.println("WTF!!!");
    #endif
    ESP.restart();
    break;
  }
} // end of switch
} // end of function
