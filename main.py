import time
import sys
sys.path.append('/csc2106-project')
import json
from mqtt_client import get_client, publish
from config import MQTT_TOPIC

# Just a placeholder for now
def read_sensors():
    pass

def main():
    # Take note there's a limit of 100 sessions, each run constitutes as one session so use this wisely. 
    client = get_client()
    try:
        payload = json.dumps({"message": "hello from Pico W"})
        publish(client, MQTT_TOPIC, payload)
    except Exception as e:
        print("Publish failed:", e)
   
main()
