#!/usr/bin/env python2.7

import paho.mqtt.client as mqtt

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

    client.subscribe("/vvm/visitorcounter/#")

def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    
client = mqtt.Client("vvmweb")
client.on_connect = on_connect
client.on_message = on_message

client.username_pw_set('username', 'password')
client.connect("broker-hostname", 1883, 60)
client.loop_forever()
