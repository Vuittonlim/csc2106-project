from machine import Pin
import utime
import dht

# ── SENSOR PINS SETUP ─────────────────────────────────────────
# HC-SR04 Ultrasonic Sensor
trigA = Pin(2, Pin.OUT) # trigger pin for sensor A (outside)
echoA = Pin(3, Pin.IN)  # echo pin for sensor A
trigB = Pin(6, Pin.OUT) # trigger pin for sensor B (inside)
echoB = Pin(7, Pin.IN)  # echo pin for sensor B

# PIR motion sensor
pir        = Pin(26, Pin.IN) # detects movement

# DHT22 temperature & humidity sensor
dht_sensor = dht.DHT22(Pin(0))

# ── STATE VARIABLES ───────────────────────────────────────────
people = 0           
state = "IDLE"       # state machine initial state
state_timer = None   # timer to reset state after timeout
TIMEOUT_MS = 10000   # reset to IDLE if no further triggers after 10s

temp, humidity = None, None
last_dht_read = utime.ticks_ms()  # track last DHT22 reading
DHT_INTERVAL  = 3000              # read DHT22 every 3s

# Debounce counters — must trigger N times in a row to count
a_count = 0
b_count = 0
DEBOUNCE = 2  # lower = more sensitive, higher = less false triggers

last_event_time = 0
COOLDOWN_MS = 1500      # min time between counting consecutive events


# ── HC-SR04 ULTRASONIC MEASUREMENT FUNCTION ─────────────────────────
# Returns distance in cm , None if measurement fails
def measure(trig, echo):
    trig.low(); utime.sleep_us(2)
    trig.high(); utime.sleep_us(10); trig.low()
    
    # wait for echo pin to go high (signal sent)
    timeout = utime.ticks_us()
    while echo.value() == 0:
        if utime.ticks_diff(utime.ticks_us(), timeout) > 30000:
            return None
        
    start = utime.ticks_us()
    # wait for echo pin to go low (signal received)
    while echo.value() == 1:
        if utime.ticks_diff(utime.ticks_us(), start) > 30000:
            return None
        
    # calculate distance: time(us) * speed of sound / 2
    return (utime.ticks_diff(utime.ticks_us(), start) * 0.0343) / 2

# ── SENSOR CALIBRATION ────────────────────────────────────────────────
print("Calibrating... keep doorway clear for 3 seconds")
samplesA, samplesB = [], []

# Take 30 measurements for each sensor to determine baseline distance
for _ in range(30):
    dA = measure(trigA, echoA)
    dB = measure(trigB, echoB)
    if dA: samplesA.append(dA)
    if dB: samplesB.append(dB)
    utime.sleep(0.1)

# Check if sensors responded
if not samplesA or not samplesB:
    print("ERROR: Sensor not responding! Check wiring.")
    raise SystemExit

# Calculate average baseline (doorway clear)
baselineA = sum(samplesA) / len(samplesA)
baselineB = sum(samplesB) / len(samplesB)
TRIGGER_MARGIN = 20 # How much closer an obj must be to count as a trigger

print(f"Baseline A: {baselineA:.1f}cm  B: {baselineB:.1f}cm")
print("Ready! (A=outside sensor, B=inside sensor)")

# ── MAIN LOOP ────────────────────────────────────────────────
while True:

    # ── DHT22 READ ─────────────────────────────────────────────
    if utime.ticks_diff(utime.ticks_ms(), last_dht_read) > DHT_INTERVAL:
        try:
            dht_sensor.measure()                 # Trigger DHT22 measurement
            temp     = dht_sensor.temperature()
            humidity = dht_sensor.humidity()
        except Exception as e:
            print("DHT22 read failed:", e)
        last_dht_read = utime.ticks_ms()

    # ── PIR MOTION SENSOR ──────────────────────────────────────────
    # If PIR is triggered and state is idle, reset counters
    if pir.value() == 0 and state == "IDLE":
        a_count = 0
        b_count = 0
        utime.sleep(0.05)
        continue
    
    # ── ULTRASONIC MEASUREMENT ───────────────────────────────
    dA = measure(trigA, echoA)
    dB = measure(trigB, echoB)
    if dA is None or dB is None: #Skip if sensor fails
        continue

    # ── DEBOUNCE: count consecutive triggers ──────────────
    # Check if sensor reading indicates someone blocking it
    a_blocked_raw = dA < (baselineA - TRIGGER_MARGIN)
    b_blocked_raw = dB < (baselineB - TRIGGER_MARGIN)

    # increment counter if blocked, reset if not
    a_count = (a_count + 1) if a_blocked_raw else 0
    b_count = (b_count + 1) if b_blocked_raw else 0

    # confirm blockage if counter exceeds DEBOUNCE threshold
    a_blocked = a_count >= DEBOUNCE
    b_blocked = b_count >= DEBOUNCE

    # ── TIMEOUT RESET ─────────────────────────────────────
    # reset state to IDLE if waiting too long without completing trigger
    if state != "IDLE" and utime.ticks_diff(utime.ticks_ms(), state_timer) > TIMEOUT_MS:
        print("Timeout reset")
        a_count = 0
        b_count = 0
        state = "IDLE"

    # ── STATE MACHINE ─────────────────────────────────────
    if state == "IDLE":
        # detect 1st sensor triggered
        if a_blocked:
            state = "A_TRIGGERED"
            state_timer = utime.ticks_ms()
            b_count = 0   # reset B so we wait for fresh B trigger
        elif b_blocked:
            state = "B_TRIGGERED"
            state_timer = utime.ticks_ms()
            a_count = 0

    elif state == "A_TRIGGERED":
        # if B is triggered after A, it counts as ENTER
        if b_blocked:
            # Only count if enough time has passed since last event
            if utime.ticks_diff(utime.ticks_ms(), last_event_time) > COOLDOWN_MS:
                people += 1
                print(f"ENTER | People inside: {people}  |  {temp:.1f}C  {humidity:.1f}%")
                last_event_time = utime.ticks_ms()
            a_count = 0; b_count = 0
            state = "IDLE"

    elif state == "B_TRIGGERED":
        # If A is triggered after B, it counts as EXIT
        if a_blocked:
            if utime.ticks_diff(utime.ticks_ms(), last_event_time) > COOLDOWN_MS:
                people = max(0, people - 1)
                print(f"EXIT  | People inside: {people}  |  {temp:.1f}C  {humidity:.1f}%")
                last_event_time = utime.ticks_ms()
            a_count = 0; b_count = 0
            state = "IDLE"

    utime.sleep(0.05)
