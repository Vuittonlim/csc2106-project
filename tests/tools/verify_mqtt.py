// MQTT subscriber to verify Pico W messages on HiveMQ Cloud.

import paho.mqtt.client as mqtt
import ssl

BROKER   = "a765286b74694e199fc8a5bdefcf0bc1.s1.eu.hivemq.cloud"
PORT     = 8883
USER     = "xinyu"
PASSWORD = "Pass1234"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to HiveMQ Cloud!")
        print("Subscribed to sit/canteen/#")
        print("Waiting for messages... (Ctrl+C to stop)\n")
        client.subscribe("sit/canteen/#")
    else:
        print(f"Connection failed with code {rc}")

def on_message(client, userdata, msg):
    print(f"[{msg.topic}] {msg.payload.decode()}")

client = mqtt.Client(client_id="laptop_verifier")
client.username_pw_set(USER, PASSWORD)
client.tls_set(tls_version=ssl.PROTOCOL_TLS)
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT)
client.loop_forever()
