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

last_pir_time = 0
PIR_DECAY_S   = 60

LORA_FAIL_THRESHOLD = 5
lora_miss_count     = 0
use_mqtt            = False
mqtt                = None

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
        m5_data = new_m5
        print("M5 data:", m5_data)

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
                mqtt     = mqtt_client.get_client()

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

        payload_str = json.dumps(publish_data)

        if use_mqtt:
            try:
                mqtt_client.publish(mqtt, config.TOPIC, payload_str)
                print("Published via MQTT:", payload_str)
            except Exception as e:
                print("MQTT publish failed, reconnecting:", e)
                mqtt = mqtt_client.get_client()
        else:
            uart_lora.write(payload_str + '\n')
            last_sent_time = now
            print("Sent to LoRa:", payload_str)

            # Poll for ACK immediately after sending
            ack_deadline = time.time() + ACK_TIMEOUT
            while time.time() < ack_deadline:
                ack = read_lora_ack()
                if ack == "ACK":
                    print("LoRa ACK received")
                    lora_miss_count = 0
                    last_sent_time  = None
                    break
                time.sleep_ms(100)
            else:
                lora_miss_count += 1
                last_sent_time   = None
                print(f"LoRa ACK timeout ({lora_miss_count}/{LORA_FAIL_THRESHOLD})")
                if lora_miss_count >= LORA_FAIL_THRESHOLD:
                    print("LoRa failed, switching to MQTT...")
                    use_mqtt = True
                    mqtt     = mqtt_client.get_client()
        last_publish = now
        

