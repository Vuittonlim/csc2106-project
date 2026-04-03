from machine import Pin
import utime
import dht
import network
import ujson
import ntptime
from coap import send_coap_post
from config import WIFI_SSID, WIFI_PASSWORD, COAP_SERVER, COAP_PORT

# ── SENSOR PINS SETUP ─────────────────────────────────────────
# ultrasonic sensor A 
trigA = Pin(4, Pin.OUT)
echoA = Pin(5, Pin.IN)

# ultrasonic sensor B
trigB = Pin(6, Pin.OUT)
echoB = Pin(7, Pin.IN)

pir = Pin(0, Pin.IN)
dht_sensor = dht.DHT22(Pin(26))

# ── STATE VARIABLES ───────────────────────────────────────────
people = 0            # number of people currently inside
state = "IDLE"        # state machine initial state
state_timer = None    # timer to reset state after timeout
TIMEOUT_MS = 10000    # reset to IDLE if no further triggers after 10s

# DHT22 temperature & humidity sensor
temp, humidity = None, None
last_dht_read = utime.ticks_ms()
DHT_INTERVAL = 3000

# Debounce counters
# sensors must detect 2 consecutive triggers to be considered active
a_count = 0
b_count = 0
DEBOUNCE = 2

# cooldown between two events to avoid double counting
last_event_time = 0
COOLDOWN_MS = 1200

# flags to prevent double counting
last_was_enter = False
last_was_exit = False
last_direction_time = 0

# ── WIFI CONNECT ──────────────────────────────────────────────
wlan = network.WLAN(network.STA_IF)

def connect_wifi():
    wlan.active(True)
    if not wlan.isconnected():
        print("Connecting to WiFi", end="")
        wlan.connect(WIFI_SSID, WIFI_PASSWORD)
        while not wlan.isconnected():
            print(".", end="")
            utime.sleep(0.5)
    print("\nWiFi connected!", wlan.ifconfig())
    try:
        ntptime.settime()
        print("Time synced!")
    except:
        print("NTP sync failed, using device time")

# sends trigger pulse and measure echo time
def measure(trig, echo):
    trig.low(); utime.sleep_us(2)
    trig.high(); utime.sleep_us(10); trig.low()
    
    timeout = utime.ticks_us()
    while echo.value() == 0:
        if utime.ticks_diff(utime.ticks_us(), timeout) > 30000:
            return None
        
    start = utime.ticks_us()
    while echo.value() == 1:
        if utime.ticks_diff(utime.ticks_us(), start) > 30000:
            return None
        
    return (utime.ticks_diff(utime.ticks_us(), start) * 0.0343) / 2

# send event to server via COAP
def send_event(event):
    try:
        payload = ujson.dumps({
            "event":    event,
            "people":   people,
            "temp":     temp if temp is not None else -1,
            "humidity": humidity if humidity is not None else -1,
            "time":     utime.time()
        })
        send_coap_post(COAP_SERVER, COAP_PORT, "canteen", payload)
    except Exception as e:
        print("CoAP send failed:", e)

connect_wifi()

# sensor calibration
print("Calibrating... keep doorway clear for 3 seconds")
samplesA, samplesB = [], []

# collect baseline readings when doorway is empty
for _ in range(30):
    dA = measure(trigA, echoA)
    dB = measure(trigB, echoB)
    if dA: samplesA.append(dA)
    if dB: samplesB.append(dB)
    utime.sleep(0.1)

# check sensors responded
if not samplesA or not samplesB:
    print("ERROR: Sensor not responding! Check wiring.")
    raise SystemExit

# compute average baseline distance
baselineA = sum(samplesA) / len(samplesA)
baselineB = sum(samplesB) / len(samplesB)

# trigger threshold
# sensor must read 25% closer than baseline to count as blocked
TRIGGER_PERCENT = 0.25

print(f"Baseline A: {baselineA:.1f}cm  B: {baselineB:.1f}cm")
print("Ready! (A=outside sensor, B=inside sensor)")

# ── MAIN LOOP ────────────────────────────────────────────────
while True:
    
    now = utime.ticks_ms()
    
    if not wlan.isconnected():
        print("WiFi dropped! Reconnecting...")
        connect_wifi()

    # read temp and humidity
    if utime.ticks_diff(now, last_dht_read) > DHT_INTERVAL:
        try:
            dht_sensor.measure()
            temp = dht_sensor.temperature()
            humidity = dht_sensor.humidity()
        except Exception as e:
            print("DHT22 read failed:", e)
        last_dht_read = now

    # Reset direction flags after 1 second
    if last_was_enter and utime.ticks_diff(now, last_direction_time) > 1000:
        last_was_enter = False
    if last_was_exit and utime.ticks_diff(now, last_direction_time) > 1000:
        last_was_exit = False

    
    # ── PIR STUCK STATE RESET ──────────────────────────────────────
    pir_active = pir.value() == 1
    
    # if PIR detects NO motion for 3 secs but the state machine is stuck mid-detection
    # reset the state machine
    if not pir_active and state != "IDLE" and utime.ticks_diff(now, state_timer) > 3000:
        state = "IDLE"
        a_count = 0
        b_count = 0
    
    # Ultrasonic sensor readings
    dA = measure(trigA, echoA)
    dB = measure(trigB, echoB)
    if dA is None or dB is None:
        continue
    
    # check if sensor is blocked
    a_blocked_raw = dA < (baselineA * (1 - TRIGGER_PERCENT))
    b_blocked_raw = dB < (baselineB * (1 - TRIGGER_PERCENT))

    # debounce logic
    a_count = (a_count + 1) if a_blocked_raw else 0
    b_count = (b_count + 1) if b_blocked_raw else 0
    
    a_blocked = a_count >= DEBOUNCE
    b_blocked = b_count >= DEBOUNCE

    # timeout reset
    if state != "IDLE" and utime.ticks_diff(now, state_timer) > TIMEOUT_MS:
        a_count = 0
        b_count = 0
        state = "IDLE"

    # ── STATE MACHINE ─────────────────────────────────────
    if state == "IDLE":
        if a_blocked:
            state = "A_TRIGGERED"
            state_timer = now
            b_count = 0
        elif b_blocked:
            state = "B_TRIGGERED"
            state_timer = now
            a_count = 0

    # -- ENTER EVENT (A->B)
    elif state == "A_TRIGGERED":
        if b_blocked:
            # Only count if last event wasn't ENTER
            if not last_was_enter:
                if utime.ticks_diff(now, last_event_time) > COOLDOWN_MS:
                    people += 1
                    print(f"ENTER | People inside: {people}  |  {temp:.1f}C  {humidity:.1f}%")
                    send_event("ENTER")
                    last_event_time = now
                    last_was_enter = True
                    last_was_exit = False
                    last_direction_time = now
            a_count = 0
            b_count = 0
            state = "IDLE"

    # EXIT EVENT (B->A)
    elif state == "B_TRIGGERED":
        if a_blocked:
            # Only count if last event wasn't EXIT
            if not last_was_exit:
                if utime.ticks_diff(now, last_event_time) > COOLDOWN_MS:
                    if people > 0:
                        people -= 1
                        print(f"EXIT  | People inside: {people}  |  {temp:.1f}C  {humidity:.1f}%")
                        send_event("EXIT")
                        last_event_time = now
                        last_was_exit = True
                        last_was_enter = False
                        last_direction_time = now
            a_count = 0
            b_count = 0
            state = "IDLE"

    utime.sleep(0.05)
