#ifndef privacy
////////////////////////////////////////////////////////////////////////////////
// configuration.h
// by: Truglodite
// updated: 6/21/2019
//
// General configuration for EspMqttDHTbatt.
////////////////////////////////////////////////////////////////////////////////
//#define debug          // Uncomment to enable serial debug output
//#define testBoard
#ifdef testBoard
  #define testBoardVbatt  4.0015  // for proper testing, use extra of precision
  #define testBoardT      74.267
  #define testBoardH      25.242
#endif
//#define extraOutput    // Uncomment to enable RSSI & millis() output.
#define batteryMonitor   // Comment out if plugged in; disables adc stuff

// Wifi ////////////////////////////////////////////////////////////////////////
const char hostName[] =       "hostName";     // hostname
const char ssid[] =           "WifiSSID";           // Wifi SSID
const char pass[]=            "WifiPassword";       // Wifi WPA2 password
const uint8_t mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};// Wifi MAC address

// MQTT ////////////////////////////////////////////////////////////////////////
IPAddress brokerIP(192,168,0,1);
const int brokerPort =         1883;
const char mqttClientID[] =    "MY_MQTT_UNIQUE_ID";
const char mqttClientUser[] =  "MY_MQTT_USERNAME";
const char mqttClientPass[] =  "MY_MQTT_PASSWORD";

// output topic example: "MY_MQTT_UNIQUE_ID/ota"
#define otaTopicD              "ota"        // FW OTA: 0 = Normal, 1 = On
#define tempTopicD             "temp"       // Dallas temperature [F]
#define humidTopicD            "humid"      // Relative Humidity [%]
#define battTopicD             "batt"       // Battery voltage
#define repubsMax        5                  // max # retries before reset

// Special payloads
const char notifyDHTfail[] =   "DHT nan";
const char notifyOTAtimeout[] ="OTA timeout";
const char notifyOTAlowbatt[] ="Low batt, OTA cancelled";
const char notifyOTAreadyB[] = "https://";  // OTA ready message+IP+path

// OTA /////////////////////////////////////////////////////////////////////////
const char update_username[] = "username";         // OTA username
const char update_password[] = "password";         // OTA password
const char update_path[] =     "/firmware";        // OTA directory
const char ntpServer1[] =      "pool.time.org";    // primary time server
const char ntpServer2[] =      "time.nist.gov";    // secondary time server
#define timeZone          -8
/*
Install openssl and use this great one-liner to create your own
self signed TLS v3 key/cert pair, then paste the contents between
the 'begin' and 'end' lines below:
  openssl req -x509 -nodes -newkey rsa:2048 -keyout serverKey.pem -sha256 -out serverCert.pem -days 4000 -subj "/C=US/ST=CA/L=province/O=Anytown/CN=CommonName"
*/
static const char serverCert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)EOF";

static const char serverKey[] PROGMEM =  R"EOF(
-----BEGIN PRIVATE KEY-----

-----END PRIVATE KEY-----
)EOF";

// Battery Monitor /////////////////////////////////////////////////////////////
#ifdef batteryMonitor
  // Input voltage @1.0V output (4.0-4.4 for 330k-100k with one L-ion cell, calibrate for accuracy)
  float vbattRatio =        4.237;
  float vbattLow =          3.75;   // low voltage mode
  float vbattCrit =         3.6;    // "all deepsleep" mode
#endif

// DHT /////////////////////////////////////////////////////////////////////////
#define dhtType             DHT22   // DHT22, DHT11, etc...
#define retriesMax          5       // nans before reboot
#define dhtPin              4       // DHT sensor data pin (default io4)
#define dhtPowerPin         5       // DHT sensor power pin (default io5)

// Timers //////////////////////////////////////////////////////////////////////
#define sleepMicros      600000000  // uSec sleep between pubs (60000000 = 10min default)
#define longSleepMicros  4294967295 // uSec sleep between low battery pubs (max ~70min)
#define dhtWarmup        2000    // mSec DHT warmup before reading
#define dhtTimeout       2000    // mSec for DHT to read before deepsleep (prevents excessive 'NAN' outputs)
#define otaTimeout       300000  // mSec to wait for OTA before reboot
#define connectTimeout   5000    // mSec to wait for OTA before reboot
#define subTimeout       5000    // mSec to wait for OTA before reboot
#define repubDelay       500     // msec minimum time between repubs

#endif
