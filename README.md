# vvm-visitorcounter

This projects contains an Arduino sketch for the
[ESP8299](https://github.com/esp8266/Arduino) Wifi module that counts pulses
from a light barrier connected to the module and posts the counter value to a MQTT
broker.

The broker hostname, login username and password are configured in the sketch.

The pulse input is defined there as well, and defaults to GPIO2.  This allows using an
ESP-01 module.

For feedback, the sketch switches the primary Serial from GPIO1 and GPIO3, and uses GPIO1
to indicate activity.  On ESP-01 modules, GPIO1/TXD is connected to a blue LED.  While
starting up, the LED will blink with 1Hz while a connection to the Wifi network is
established.  Once a connection has been established, the LED will be on.  It will shortly
turn off once a second as a heartbeat, and turn off while the GPIO2 input is low (active).

The circuit used to connect the light barrier to the module is in
[schematics](schematics).

## MQTT Messages

The sketch will post the following messages.  The sketch will post the messages regularly
(approximately every 60 seconds).

### /vvm/visitorcounter/counter

The current counter value.  The message is posted with retain=true, so new clients will
receive the current counter value immediately.  

### /vvm/visitorcounter/uptime

Seconds since the module booted.  The value might not be accurate, but allows a client
to gauge whether the module is active and working.  The module registers this message
with a value of 0 as its last-will-and-testament, so when the module goes offline,
the broker will post an uptime of 0.  Both messages are posted with retain=true, so
clients connecting to the broker will learn the current status of the module immediately.

## Test Client

As a quick way to see messages posted by the sketch, you can use the Paho test client in
[paho-testclient](paho-testclient).  To set up your Python, you should use 
[Virtualenv](https://virtualenv.pypa.io/en/latest/).

Example setup:
```bash
virtualenv paho-testclient
source paho-testclient/bin/activate
pip install paho-mqtt
paho-testclient/testclient.py
```
