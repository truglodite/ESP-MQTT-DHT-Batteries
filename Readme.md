# ESP-MQTT-DHT-Batteries
for: ESP8266 + DHTXX + deepsleep + battery monitor + OTA + MQTT
by: Truglodite

## General Operation
Deepsleeps, wakes every "sleepMicros" (every ~10min default), and sends "temp" (F), "humid" (%), and "batt" (V) to a MQTT broker (Wifi RSSI (dBm) optional). While awake, it subs to "ota" to initiate browser OTA mode (TLS secured). It also subs to "temp", "humid", and "batt" to verify published data was properly recieved by the broker; if any published (local) data doesn't match broker (server) data, all data is republished and rechecked (up to 5 repetitions by default... think of it like "arduino qos1"). If battery voltage drops below "vbattLow", data and a notification are sent every 70min by default. When the battery voltage reads below "vbattCrit", data and a notification are no longer sent, and the esp goes to "deepsleep(0)", where it will not wake again until the battery is removed/replaced.

## MQTT Pubs & Subs
*Note: All topics (both pubs and subs) use the format "client_id/topic". Device name is hardcoded in the configuration file.*

S/P | Topic | Data
--- | ------ | ---------------
Sub | "ota" | Firmware Upload [0 = normal, 1 = upload]
Sub | "batt" | Battery voltage (x.xx)
Sub | "temp" | Temperature [F]
Sub | "humid" | Relative Humidity [%]
Pub | "batt" | Battery voltage (x.xx)
Pub | "temp" | Temperature [F]
Pub | "humid" | Relative Humidity [%]

## ESP8266 Pins
ESPpin | Description
------ | -------------------
io4 | DHT sensor data pin
io5 | DHT Vcc pin
A0 | Battery voltage divider output (default 330k/100k with 1s Li-Ion)
EN | 10k High
io2 | 10k High
io15 | 10k Low
io16 | RST - 220ohm resistor link for self-wake from deepsleep

## Install
Use platformio, arduino ide, or other to edit configuration.h, compile, and upload to your esp8266, nodemcu, wemos d1, etc board. To create self signed SSL certificates in Windows (for browser OTA), install openssl, 'cd' to some directory you'll remember, and use the one liner below at the command prompt.
```
openssl req -x509 -nodes -newkey rsa:2048 -keyout serverKey.pem -sha256 -out serverCert.pem -days 4000 -subj "/C=US/ST=CA/L=province/O=Mycity/CN=deviceIPaddress"
```

## OTA Firmware Updates
This code makes use of an SSL secured webserver for OTA updates, which is controlled via MQTT messages. If a "1" is received on the "ota" topic while awake, and battery is not low, when the esp next awakens it will publish "https://deviceipaddress/firmware" to the "ota" topic, and stay in browser upgrade mode. Do not send an ota off to the device while upgrading. If no upgrades are initiated before "otaTimeout", it will reboot and resume normal mode. Be sure to publish the ota message to your broker with the retain flag set to true so this code always has a valid ota state when running. This code will attempt to download the ota subscription for subTimeout before cancelling the ota routine.

## Notes on Battery Life
Power to the DHT sensor is controlled by io5. The DHT is turned off while asleep to save battery. Note that the DHT is an appreciable capacitive load, so low quiescent LDO's may have trouble with voltage spike resets. A somewhat large bypass cap may be required for stability; note that some candidate capacitors (ie: common quality electrolytics) have very large leakage current, which can drastically reduce battery life.

Maximize deepsleep time (sleepMinutes) as much as reasonable with your application to prolong battery life. Eliminating unnecessary peripherals (ie LEDs and USB/UART chips... why wemos D1 isn't very good and bare esp's are great for batteries), and using low quiescent LDO's (ADP160AU is good), and low leakage electrolytic capacitors will further improve battery life.

## Notes DHTXX Accuracy W.R.T. deepsleep
The author of this code has observed inaccurate humidity readings from DHT sensors when the sensors are read while still 'warming up'. Since the DHT sensor is powered off during deepsleep, it takes some time for humidity readings to become accurate. This code checks at least 2 successive humidity values, and if they are far enough apart, it will continue reading until it has a new value that is close to the preceeding value.
