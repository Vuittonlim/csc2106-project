"""
Crowd monitoring software stack
--------------------------------
MQTT subscriber  →  InfluxDB writer
Payload schema: aggregated crowd data from gateway
{
  "totalPeople": 65,
  "density": 0.62,
  "score": 0.58,
  "predicted": 0.49,
  "trend": "DECREASING",
  "level": "MEDIUM",
  "bestTime": "SOON",
  "prolongedHigh": 0,
  "confidence": 1,
  "zones": {
    "Seating Zone 1": 0.55,
    "Seating Zone 2": 0.72,
    "Seating Zone 3": 0.60
  }
}
"""

import json
import ssl
import certifi
import logging
from datetime import datetime, timezone

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_USER as MQTT_USERNAME, MQTT_PASSWORD,
    MQTT_TOPICS, CLIENT_ID,
    INFLUX_URL, INFLUX_TOKEN, INFLUX_ORG, INFLUX_BUCKET,
    TELEGRAM_TOKEN,
)
from telegram_notifier import TelegramNotifier

LOG_LEVEL = logging.INFO

logging.basicConfig(
    level=LOG_LEVEL,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("crowd_stack")

# ─────────────────────────────────────────────
# InfluxDB writer
# ─────────────────────────────────────────────

class InfluxWriter:
    def __init__(self):
        self.client = InfluxDBClient(
            url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG
        )
        self.write_api = self.client.write_api(write_options=SYNCHRONOUS)
        log.info("InfluxDB client ready → %s / %s", INFLUX_URL, INFLUX_BUCKET)

    def write(self, data: dict):
        """
        Write one payload to InfluxDB.

        Measurement : crowd_status
        Tags        : level, trend, bestTime
        Fields      : totalPeople, density, score, predicted,
                      prolongedHigh, confidence, temperature, humidity,
                      zone_seating_zone_1, zone_seating_zone_2
        """
        now = datetime.now(tz=timezone.utc)

        point = (
            Point("crowd_status")
            # ── tags ─────────────────────────────────────────────────────
            .tag("level",    data.get("level",    "UNKNOWN"))
            .tag("trend",    data.get("trend",    "UNKNOWN"))
            .tag("bestTime", data.get("bestTime", "UNKNOWN"))
            # ── numeric fields ────────────────────────────────────────────
            .field("totalPeople",   int(data.get("totalPeople",   0)))
            .field("density",       float(data.get("density",     0.0)))
            .field("score",         float(data.get("score",       0.0)))
            .field("predicted",     float(data.get("predicted",   0.0)))
            .field("prolongedHigh", int(data.get("prolongedHigh", 0)))
            .field("confidence",    int(data.get("confidence",    0)))
            .field("temperature",   float(data.get("avgTemp", 0.0)))
            .time(now)
        )

        # ── one field per zone ────────────────────────────────────────────
        for zone_name, zone_val in data.get("zones", {}).items():
            field_key = "zone_" + zone_name.lower().replace(" ", "_")
            point = point.field(field_key, str(zone_val))

        self.write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)

        log.info(
            "Wrote → people=%-3d  density=%.2f  level=%-6s  trend=%-10s  t=%.1f°C  zones=%s",
            data.get("totalPeople", 0),
            data.get("density", 0),
            data.get("level", "?"),
            data.get("trend", "?"),
            data.get("avgTemp", 0),
            data.get("zones", {}),
        )

    def close(self):
        self.client.close()

# ─────────────────────────────────────────────
# MQTT subscriber
# ─────────────────────────────────────────────

class CrowdSubscriber:
    def __init__(self, writer: InfluxWriter, notifier: TelegramNotifier):
        self.writer   = writer
        self.notifier = notifier
        self.client = mqtt.Client(
            client_id=CLIENT_ID,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2
        )
        self.client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
        self.client.tls_set(
            ca_certs=certifi.where(),
            tls_version=ssl.PROTOCOL_TLS_CLIENT
        )
        self.client.on_connect    = self._on_connect
        self.client.on_message    = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            log.info("Connected to MQTT broker %s:%s", MQTT_BROKER, MQTT_PORT)
            for topic, qos in MQTT_TOPICS:
                client.subscribe(topic, qos)
                log.info("Subscribed → %s  (QoS %d)", topic, qos)
        else:
            log.error("MQTT connect failed, rc=%s", reason_code)

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        if reason_code != 0:
            log.warning("Unexpected MQTT disconnect rc=%s — will auto-reconnect", reason_code)

    def _on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload.decode("utf-8"))
            log.debug("Raw message on %s: %s", msg.topic, data)

            required = {"totalPeople", "density", "level", "trend"}
            missing  = required - data.keys()
            if missing:
                log.warning("Payload missing fields: %s — skipping", missing)
                return
            self.writer.write(data)
            self.notifier.send(data)

        except json.JSONDecodeError as exc:
            log.warning("Bad JSON on %s: %s  raw=%r", msg.topic, exc, msg.payload[:120])
        except Exception as exc:
            log.error("Error processing %s: %s", msg.topic, exc, exc_info=True)

    def run(self):
        self.client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        log.info("Starting MQTT loop (Ctrl-C to stop)")
        try:
            self.client.loop_forever()
        except KeyboardInterrupt:
            log.info("Shutting down…")
        finally:
            self.client.disconnect()
            self.writer.close()

# ─────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────

if __name__ == "__main__":
    writer     = InfluxWriter()
    notifier   = TelegramNotifier(TELEGRAM_TOKEN)
    notifier.start_polling()
    subscriber = CrowdSubscriber(writer, notifier)
    subscriber.run()