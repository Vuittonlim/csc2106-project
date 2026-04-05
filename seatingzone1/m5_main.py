import M5
from M5 import Lcd, Mic
from machine import UART
import array
import time
import json
import math

uart = UART(1, baudrate=115200, tx=26, rx=0)

SOUND_LOW_THRESHOLD  = 800
SOUND_HIGH_THRESHOLD = 1400
HISTORY_SIZE = 5
rms_history = []

def read_sound_level():
    global rms_history
    Mic.end()
    Mic.config("magnification", 1)
    Mic.begin()
    time.sleep_ms(500)
    buf = array.array('h', [0] * 8000)
    Mic.record(buf, 8000, False)
    time.sleep(1)
    rms = math.sqrt(sum(x * x for x in buf) / len(buf))

    rms_history.append(rms)
    if len(rms_history) > HISTORY_SIZE:
        rms_history.pop(0)
    smoothed = sum(rms_history) / len(rms_history)

    if smoothed < SOUND_LOW_THRESHOLD:
        return "L", int(smoothed)
    elif smoothed < SOUND_HIGH_THRESHOLD:
        return "M", int(smoothed)
    else:
        return "H", int(smoothed)

def update_display(sound_lvl, rms):
    Lcd.clear()
    Lcd.setCursor(0, 0)
    Lcd.print("Zone1 Seating\n")
    Lcd.print("Sound: " + sound_lvl + "\n")
    Lcd.print("RMS:   " + str(int(rms)) + "\n")
    Lcd.print("TX: OK")

M5.begin()
print("Zone 1 M5StickC Plus starting (RMS mode)...")

while True:
    sound_lvl, rms = read_sound_level()
    payload = {"s": sound_lvl, "r": int(rms)}
    line = json.dumps(payload) + "\n"
    uart.write(line.encode())
    print("Sent: " + line.strip())
    update_display(sound_lvl, rms)
    time.sleep(1)

