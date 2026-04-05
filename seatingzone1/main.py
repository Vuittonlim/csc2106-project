import json
import time
import dht
import sys
sys.path.append('/csc2106-project')
from machine import Pin, UART
import config
import mqtt_client

PIR_PIN = Pin(14, Pin.IN)
DHT_PIN = dht.DHT22(Pin(6))
uart      = UART(1, baudrate=115200, tx=Pin(4), rx=Pin(5))
uart_lora = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1))

# Initialise to current time so the first 60s doesn't misread PIR state
last_pir_time = time.time()
PIR_DECAY_S   = 60

LORA_FAIL_THRESHOLD = 5
lora_miss_count     = 0
use_mqtt            = False
mqtt                = None

M5_STALE_THRESHOLD = 30  # seconds before m5_data is considered stale
m5_last_updated    = None

MQTT_PING_INTERVAL = 30  # seconds between keepalive pings
last_ping_time     = 0

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
        temp  = round(DHT_PIN.temperature(), 1)
        humid = round(DHT_PIN.humidity(), 1)
        return temp, humid
    except Exception as e:
        print("DHT22 error:", e)
        return None, None

uart_buf  = ""
lora_buf  = ""

def read_uart_m5():
    global uart_buf
    if uart.any():
        try:
            chunk = uart.read(uart.any()).decode()
            uart_buf += chunk
            if '\n' in uart_buf:
                line, uart_buf = uart_buf.split('\n', 1)
                return json.loads(line.strip())
        except Exception as e:
            print("UART parse error:", e)
            uart_buf = ""
    return None

def read_lora_ack():
    global lora_buf
    if uart_lora.any():
        try:
            chunk = uart_lora.read(uart_lora.any()).decode()
            print("LoRa raw:", repr(chunk))
            lora_buf += chunk
            if '\n' in lora_buf:
                line, lora_buf = lora_buf.split('\n', 1)
                return line.strip()
        except Exception as e:
            print("LoRa UART read error:", e)
            lora_buf = ""
    return None

def fuse_crowd(m5_data, pir_active):
    if m5_data is None:
        return "M" if pir_active else "L"
    sound = m5_data.get("s", "L")
    if not pir_active:
        return "M" if sound == "H" else "L"
    if sound == "H":
        return "H"
    return "M"

def try_get_mqtt():
    try:
        return mqtt_client.get_client()
    except RuntimeError as e:
        print("MQTT connection failed:", e)
        return None

print("Zone 1 Pico W starting...")
time.sleep(2)

PUBLISH_INTERVAL = 10
last_publish     = time.time()
last_sent_time   = None  # when we last sent a LoRa packet
ACK_TIMEOUT      = 5     # seconds to wait for ACK
m5_data          = None

while True:
    now = time.time()

    new_m5 = read_uart_m5()
    if new_m5:
        m5_data         = new_m5
        m5_last_updated = now
        print("M5 data:", m5_data)

    # Expire stale m5_data if M5StickC has gone silent
    if m5_data is not None and m5_last_updated is not None:
        if now - m5_last_updated > M5_STALE_THRESHOLD:
            print("M5 data stale, discarding")
            m5_data         = None
            m5_last_updated = None

    pir = read_pir()

    # Check for LoRa ACK if we're waiting for one
    if not use_mqtt and last_sent_time is not None:
        ack = read_lora_ack()
        if ack == "ACK":
            print("LoRa ACK received")
            lora_miss_count = 0
            last_sent_time  = None
        elif now - last_sent_time > ACK_TIMEOUT:
            lora_miss_count += 1
            last_sent_time   = None
            print(f"LoRa ACK timeout ({lora_miss_count}/{LORA_FAIL_THRESHOLD})")
            if lora_miss_count >= LORA_FAIL_THRESHOLD:
                print("LoRa failed, switching to MQTT...")
                use_mqtt = True
                mqtt = try_get_mqtt()
                if mqtt is None:
                    print("MQTT fallback also failed, retrying LoRa")
                    use_mqtt = False
                    lora_miss_count = 0

    # MQTT keepalive ping — rate limited to once per MQTT_PING_INTERVAL
    if use_mqtt and mqtt is not None and (now - last_ping_time >= MQTT_PING_INTERVAL):
        try:
            mqtt.ping()
            last_ping_time = now
        except Exception as e:
            print("MQTT ping failed, reconnecting:", e)
            mqtt = try_get_mqtt()
            if mqtt is not None:
                last_ping_time = now

    if now - last_publish >= PUBLISH_INTERVAL:
        temp, humid = read_dht()
        crowd = fuse_crowd(m5_data, pir)
        publish_data = {
            "zone":  "seating_1",
            "c":     crowd,
            "pir":   pir,
            "t":     temp,
            "h":     humid,
            "proto": "M" if use_mqtt else "L"
        }
        if m5_data:
            publish_data["s"] = m5_data.get("s")
            publish_data["r"] = m5_data.get("r")

        payload_str = (
            '{{"zone":"{zone}","c":"{c}","pir":{pir},"t":{t},"h":{h},"proto":"{proto}"{sound}}}'
            .format(
                zone=publish_data["zone"],
                c=publish_data["c"],
                pir=publish_data["pir"],
                t=publish_data["t"],
                h=publish_data["h"],
                proto=publish_data["proto"],
                sound=(',"s":"{s}","r":{r}'.format(s=publish_data["s"], r=publish_data["r"]) if m5_data else "")
            )
        )

        if use_mqtt:
            if mqtt is None:
                print("MQTT client unavailable, attempting reconnect...")
                mqtt = try_get_mqtt()
            if mqtt is not None:
                try:
                    mqtt_client.publish(mqtt, config.TOPIC, payload_str)
                    print("Published via MQTT:", payload_str)
                except Exception as e:
                    print("MQTT publish failed, reconnecting:", e)
                    mqtt = try_get_mqtt()
            else:
                print("Skipping publish, no MQTT client available")
        else:
            uart_lora.write(payload_str + '\n')
            last_sent_time = now
            print("Sent to LoRa:", payload_str)
        last_publish = now

