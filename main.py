import json
import time
import dht
import sys
sys.path.append('/csc2106-project')
from machine import Pin, UART
import config
import mqtt_client

PIR_PIN = Pin(14, Pin.IN)
DHT_PIN = dht.DHT22(Pin(15))
uart    = UART(1, baudrate=9600, tx=Pin(4), rx=Pin(5))

last_pir_time = 0
PIR_DECAY_S   = 60 #treat zone as active for 60s after last PIR trigger

def read_pir():
    global last_pir_time
    val = PIR_PIN.value()
    if val == 1:
        last_pir_time = time.time()
    elapsed = time.time() - last_pir_time
    return 1 if elapsed < PIR_DECAY_S else 0

def read_dht():
    try:
        DHT_PIN.measure()
        return DHT_PIN.temperature(), DHT_PIN.humidity()
    except Exception as e:
        print("DHT22 error:", e)
        return None, None

def read_uart_m5():
    if uart.any():
        try:
            line = uart.readline()
            if line and b'\n' in line:
                return json.loads(line.decode().strip())
        except Exception as e:
            print("UART parse error:", e)
    return None

def fuse_crowd(m5_data, pir_active):
    if m5_data is None: # If no data from M5 then fall back to PIR
        return ("M" if pir_active else "L"), 0
    sound = m5_data.get("s", "L")
    if not pir_active and sound == "H":  # If no PIR movement but lots of noise, it will be medium
        sound = "M"
        return sound, 0
    if pir_active and sound in ("M", "H"):
        return sound, 1
    return sound, 0

print("Zone 1 Pico W starting...")
time.sleep(2)

mqtt = mqtt_client.get_client()

PUBLISH_INTERVAL = 10
last_publish     = time.time()
m5_data          = None

while True:
    now = time.time()

    new_m5 = read_uart_m5()
    if new_m5:
        m5_data = new_m5
        print("M5 data:", m5_data)

    pir = read_pir()

    if now - last_publish >= PUBLISH_INTERVAL:
        temp, humid = read_dht()
        crowd, q    = fuse_crowd(m5_data, pir)

        publish_data = {
            "zone": "seating_1",
            "c":    crowd,
            "q":    q,
            "pir":  pir,
            "t":    temp,
            "h":    humid,
        }

        if m5_data:
            publish_data["s"] = m5_data.get("s")
            publish_data["r"] = m5_data.get("r")

        payload_str = json.dumps(publish_data)

        try:
            mqtt_client.publish(mqtt, config.MQTT_TOPIC, payload_str.encode())
        except Exception as e:
            print("Publish failed, reconnecting:", e)
            try:
                mqtt = mqtt_client.get_client()
            except Exception as e2:
                print("Reconnect failed:", e2)

        last_publish = now

    time.sleep(1)
