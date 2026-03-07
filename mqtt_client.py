import time
import network
from umqtt.simple import MQTTClient
from config import (
    USE_LOCAL_BROKER,
    WIFI_SSID, WIFI_PASSWORD,
    MQTT_BROKER, MQTT_PORT,
    MQTT_USER, MQTT_PASSWORD,
    LOCAL_BROKER, LOCAL_PORT,
    CLIENT_ID
)

def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)

    if wlan.isconnected():
        print("WiFi already connected:", wlan.ifconfig())
        return wlan

    print("Connecting to WiFi...")
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)

    timeout = 15
    while not wlan.isconnected() and timeout > 0:
        time.sleep(1)
        timeout -= 1

    if not wlan.isconnected():
        raise RuntimeError("WiFi connection failed")

    print("WiFi connected:", wlan.ifconfig())
    return wlan


def connect_mqtt():
    if USE_LOCAL_BROKER:
        print("Connecting to local Mosquitto broker...")
        client = MQTTClient(
            client_id=CLIENT_ID,
            server=LOCAL_BROKER,
            port=LOCAL_PORT
        )
    else:
        print("Connecting to HiveMQ Cloud...")
        client = MQTTClient(
            client_id=CLIENT_ID,
            server=MQTT_BROKER,
            port=MQTT_PORT,
            user=MQTT_USER,
            password=MQTT_PASSWORD,
            ssl=True,
            ssl_params={"server_hostname": MQTT_BROKER}
        )
        
    retries = 0
    max_retries = 5

    while retries < max_retries:
        try:
            client.connect()
            broker = LOCAL_BROKER if USE_LOCAL_BROKER else MQTT_BROKER
            print("MQTT connected to", broker)
            return client
        except Exception as e:
            retries += 1
            print(f"MQTT connection failed ({retries}/{max_retries}):", e)
            time.sleep(5)

    raise RuntimeError("MQTT connection failed after max retries")

def publish(client, topic, payload, retain=False):
    try:
        client.publish(topic, payload, retain=retain)
        print(f"Published to {topic}: {payload}")
    except Exception as e:
        print("Publish failed:", e)
        raise
    
def get_client():
    connect_wifi()
    return connect_mqtt()
