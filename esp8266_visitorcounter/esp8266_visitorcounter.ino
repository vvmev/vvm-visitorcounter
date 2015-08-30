/*
 * Copyright (c) 2015, Stefan Bethke
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

//#define DEBUG
#if defined(DEBUG)
#define DEBUGP(x)  Serial.print(x)
#define DEBUGPLN(x) Serial.println(x)
#else
#define DEBUGP(x)
#define DEBUGPLN(x)
#endif

int pinCounter = 2;
int pinLED = 1;
const char *brokerName = "broker-hostname";
const char *brokerUser = "username";
const char *brokerPass = "password";

unsigned long counter = 0;
volatile unsigned int cycles = 0;

WiFiClient wifiClient;
PubSubClient pubSubClient(wifiClient, brokerName);

ESP8266WebServer server(80);


/*
 * Returns the difference between two timestamps, with adjustment for potential
 * wrap-around.
 */
static unsigned long millisElapsed(unsigned long earlier, unsigned long later) {
  if (earlier > later) {
    earlier += 2147483648L;
    later += 2147483648L;
  }
  return later - earlier;
}


/**
 * Deliver an error page.
 */
void webHandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}


/**
 * Deliver a basic page.
 */
void webHandleRoot() {
  char buffer[256];
  unsigned long now = millis() / 1000;
  
  snprintf(buffer, sizeof(buffer), "<html><head><title>Visitor Counter</title></head><body>"
      "<h1>Visitor Counter</h1>"
      "<p>counter = %ld</p>"
      "<p>uptime = %d:%02d:%02d</p>"
      "<p>built " __DATE__ ", " __TIME__ "</p>"
      "</body></html>",
    counter, now/3600, now/60 % 60, now % 60);
  server.send(200, "text/html", buffer);
}


/**
 * Interupt handler that counts the cycles detected by the opto-coupler.
 */
void counterInterrupt() {
  cycles++;
}


/**
 * Detect a proper counter signal. Using the cycle counter that is updated by the interrupt
 * handler, wait for at least one cycle, then wait for a pause of at least 0.5s. Finally,
 * increment and publish the counter.
 */
void debounceCounter() {
  static unsigned long last = 0;
  static unsigned long cycle = 0;

  if (cycles == 0)
    return;
  if (cycles == cycle) {
    if (millisElapsed(last, millis()) > 500) {
      //pubSubClient.publish("/vvm/visitorcounter/cycles", String(cycles));
      last = millis();
      cycle = 0;
      cycles = 0;
      counter++;
      publishCounter();
    }
  } else {
    last = millis();
    cycle = cycles;
  }
}


/**
 * Save the current counter value to EEPROM. Wait for the value to not
 * change anymore before updating the EEPROM to save on write cycles.
 */
void saveCounter() {
  static unsigned long last = 0, lastCounter = 0;
  unsigned long eeprom;

  if (counter != lastCounter) {
    last = millis();
    lastCounter = counter;
    return;
  }
  if (millisElapsed(last, millis()) > 60000) {
    last = millis();
    EEPROM.get(0, eeprom);
    if (eeprom != counter) {
      EEPROM.put(0, counter);
      EEPROM.commit();
      DEBUGPLN("counter saved to eeprom.");
    }
  }
}


/**
 * Publish the current counter value.
 */
void publishCounter() {
  MQTT::Publish publish("/vvm/visitorcounter/counter", String(counter));
  publish.set_retain(true);
  pubSubClient.publish(publish);
}


/**
 * Publish the current uptime.
 */
void publishUptime() {
  MQTT::Publish publish("/vvm/visitorcounter/uptime", String(millis()/1000));
  publish.set_retain(true);
  pubSubClient.publish(publish);
}


/**
 * Connect to the broker and regularly post the module's uptime and the counter.
 */
void updateBroker() {
  static unsigned long lastPost = 0;
  static unsigned long lastConnect = 0;
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!pubSubClient.connected()
        && (lastConnect == 0 || millisElapsed(lastConnect, millis()) > 10000)) {
      // only try to (re-)connect every 10 seconds
      DEBUGP("Connecting to broker at ");
      DEBUGPLN(brokerName);
      MQTT::Connect connect("vvm-visitorcounter");
      connect.set_auth(brokerUser, brokerPass);
      connect.set_will("/vvm/visitorcounter/uptime", "0", 0, true);
      if (pubSubClient.connect(connect)) {
        DEBUGPLN("broker connected and message sent");
      } else {
        DEBUGPLN("Connection failed");
      }
      lastConnect = millis();
    }
    if (pubSubClient.connected()
        && (lastPost == 0 || millisElapsed(lastPost, millis()) > 60000)) {
      //pubSubClient.publish("/vvm/visitorcounter/cycles", String(cycles));
      publishUptime();
      publishCounter();
      lastPost = millis();
    }
  }
  pubSubClient.loop();
}


/**
 * Blink the blue TX LED off once a second; if the opto-coupler is active,
 * turn off the LED.
 */
void heartbeat() {
  static unsigned long last = 1;
  static int state = 0;
  if (state) {
    digitalWrite(pinLED, 1);
    if (millisElapsed(last, millis()) > 100) {
      last = millis();
      state = 0;
    }
  } else {
    digitalWrite(pinLED, !digitalRead(pinCounter));
    if (millisElapsed(last, millis()) > 900) {
      last = millis();
      state = 1;
    }
  }
}


void setup() {
  Serial.begin(115200);

#if !defined(DEBUG)
  Serial.swap();
#endif

  DEBUGPLN("");
  DEBUGPLN("");
  DEBUGPLN("VVM Visitor Counter " __DATE__ " " __TIME__ " starting up");

  //WiFi.begin("ssid", "key");

  pinMode(pinCounter, INPUT);
  pinMode(pinLED, OUTPUT);
  digitalWrite(pinLED, 0);

  EEPROM.begin(4);
  EEPROM.get(0, counter);
  DEBUGP("Counter loaded from EEPROM: ");
  DEBUGPLN(counter);

  DEBUGP("Logging on to \"");
  DEBUGP(WiFi.SSID());
  DEBUGP("\" ");
    while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(pinLED, !digitalRead(pinLED));
    delay(500);
    DEBUGP(".");
  }
  DEBUGPLN(" connected.");
  DEBUGP("IP: ");
  DEBUGPLN(WiFi.localIP());

  server.onNotFound(webHandleNotFound);
  server.on("/", webHandleRoot);
  server.begin();

  updateBroker();

  attachInterrupt(pinCounter, counterInterrupt, FALLING);

  DEBUGPLN("Setup complete.");
  digitalWrite(pinLED, 0);
}


void loop() {
  server.handleClient();
  updateBroker();
  debounceCounter();
  saveCounter();
  heartbeat();
}

