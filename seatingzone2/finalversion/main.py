# Pico W MQTT Bridge

import network
import ujson
import time
import ssl
from machine import UART, Pin, Timer
from umqtt.simple import MQTTClient

# credentials
WIFI_SSID     = ""       
WIFI_PASSWORD = ""

# HiveMQ Cloud credentials
MQTT_BROKER   = "a765286b74694e199fc8a5bdefcf0bc1.s1.eu.hivemq.cloud"
MQTT_PORT     = 8883
MQTT_USER     = "xinyu"
MQTT_PASSWORD = "Pass1234"
MQTT_CLIENT   = "PicoW_Bridge"

TOPIC_ZONE    = "sit/canteen/zone/2"
TOPIC_STATUS  = "sit/canteen/status/m5stick"

# LED
led = Pin("LED", Pin.OUT)

# UART M5StickC
uart = UART(0, baudrate=9600, tx=Pin(0), rx=Pin(1))

# WiFi 
def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        print("WiFi already connected:", wlan.ifconfig()[0])
        return wlan

    print(f"Connecting to WiFi '{WIFI_SSID}'...")
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)

    timeout = 15
    while not wlan.isconnected() and timeout > 0:
        time.sleep(1)
        timeout -= 1
        print(".", end="")

    if wlan.isconnected():
        print(f"\nWiFi connected: {wlan.ifconfig()[0]}")
        led.on()
    else:
        print("\nWiFi connection failed!")
        led.off()
        raise RuntimeError("WiFi failed")

    return wlan


# MQTT over TLS
mqtt = None

def connect_mqtt():
    global mqtt
    # TLS context, HiveMQ Cloud requires SSL
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.verify_mode = ssl.CERT_NONE

    mqtt = MQTTClient(
        MQTT_CLIENT,
        MQTT_BROKER,
        port=MQTT_PORT,
        user=MQTT_USER,
        password=MQTT_PASSWORD,
        ssl=ctx,
        keepalive=60,
    )

    print(f"Connecting to MQTT broker {MQTT_BROKER}:{MQTT_PORT}...")
    # Set Last Will, mark offline if Pico W disconnects unexpectedly
    mqtt.set_last_will(TOPIC_STATUS, "offline", retain=True, qos=1)
    mqtt.connect()
    mqtt.publish(TOPIC_STATUS, "online", retain=True, qos=1)
    print("MQTT connected!")


def ensure_mqtt():
    # Reconnect MQTT if needed
    global mqtt
    try:
        mqtt.ping()
    except Exception:
        print("MQTT lost, reconnecting...")
        try:
            connect_mqtt()
        except Exception as e:
            print(f"MQTT reconnect failed: {e}")


# UART 
def read_uart_line():
    
    if uart.any():
        try:
            raw = uart.readline()
            if raw:
                line = raw.decode("utf-8").strip()
                if line.startswith("{"):
                    data = ujson.loads(line)
                    return data
        except Exception as e:
            print(f"UART parse error: {e}")
    return None


# Expand labels for readability
SOUND_MAP = {"L": "Low", "M": "Medium", "H": "High"}
CROWD_MAP = {"L": "Low", "M": "Medium", "H": "High"}

def build_payload(data):
    # Incoming from M5:  {"s":"H","b":12,"c":"M","q":1}
    # Outgoing to cloud: {"zone":"seating_2","sound":"High","ble":12,"crowd":"Medium","confidence":1}
    return ujson.dumps({
        "zone":       "seating_2",
        "sound":      SOUND_MAP.get(data.get("s", "L"), "Low"),
        "ble":        data.get("b", 0),
        "crowd":      CROWD_MAP.get(data.get("c", "L"), "Low"),
        "confidence": data.get("q", 1),
    })


# Main
def main():
    connect_wifi()
    connect_mqtt()

    print("Bridge running - waiting for UART data from M5StickC...\n")

    heartbeat = time.ticks_ms()

    while True:
        data = read_uart_line()

        if data:
            payload = build_payload(data)
            try:
                mqtt.publish(TOPIC_ZONE, payload, retain=True, qos=0)
                print(f"Published: {payload}")
                led.toggle()  # blink on each publish
            except Exception as e:
                print(f"Publish failed: {e}")
                ensure_mqtt()

        # Periodic keepalive every 30s
        now = time.ticks_ms()
        if time.ticks_diff(now, heartbeat) > 30_000:
            heartbeat = now
            ensure_mqtt()

        time.sleep_ms(100)


# Entry point
if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped by user")
        if mqtt:
            mqtt.publish(TOPIC_STATUS, "offline", retain=True, qos=1)
            mqtt.disconnect()
