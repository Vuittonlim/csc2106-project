"""
Dashboard API server
---------------------
GET /          → serves dashboard.html
GET /api/status → JSON from InfluxDB crowd_status + zone recommendation
                  falls back to mock data if InfluxDB is empty or unreachable

Run: python3 api.py
"""

import logging
from datetime import datetime, timezone

from flask import Flask, jsonify
from influxdb_client import InfluxDBClient

from config import INFLUX_URL, INFLUX_TOKEN, INFLUX_ORG, INFLUX_BUCKET

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("api")

app = Flask(__name__, static_folder=".", static_url_path="")

# ─────────────────────────────────────────────
# Zone config
# ─────────────────────────────────────────────

ZONE_FIELDS = [
    "zone_1",
    "zone_2",
]
ZONE_LABELS = {
    "zone_1": "Seating Zone 1",
    "zone_2": "Seating Zone 2",
}

NO_DATA_RESPONSE = {
    "live":    False,
    "message": "No data available — waiting for sensor readings.",
}




# ─────────────────────────────────────────────
# InfluxDB query
# ─────────────────────────────────────────────

def fetch_latest() -> dict | None:
    """
    Pull the most recent crowd_status record from InfluxDB.
    Returns a flat dict of all fields + tags, or None if no data.
    """
    client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    try:
        flux = f"""
from(bucket: "{INFLUX_BUCKET}")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "crowd_status")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> sort(columns: ["_time"], desc: true)
  |> limit(n: 1)
"""
        tables = client.query_api().query(flux, org=INFLUX_ORG)
        rows = [r.values for t in tables for r in t.records]
        if not rows:
            return None
        # Pick the globally latest row across all tag-series groups
        return max(rows, key=lambda r: r.get("_time", datetime.min.replace(tzinfo=timezone.utc)))
    finally:
        client.close()

# ─────────────────────────────────────────────
# Zone helpers
# ─────────────────────────────────────────────

def _label(density: float) -> str:
    if density < 0.4: return "L"
    if density < 0.7: return "M"
    return "H"


LEVEL_ORDER = {"low": 0, "medium": 1, "high": 2}

def _recommend(zones: list[dict]) -> dict:
    ranked = sorted(zones, key=lambda z: LEVEL_ORDER.get(z["level"].lower(), 9))
    best   = ranked[0]
    alts   = [z["zone"] for z in ranked[1:] if z["level"].lower() != "high"]
    return {
        "preferred_zone": best["zone"],
        "reason":         f"{best['zone']} is {best['level']} crowd",
        "alternatives":   alts,
        "alerts":         [],
    }

# ─────────────────────────────────────────────
# Routes
# ─────────────────────────────────────────────

@app.route("/")
def index():
    return app.send_static_file("dashboard.html")


@app.route("/api/status")
def status():
    try:
        row = fetch_latest()
    except Exception as exc:
        log.warning("InfluxDB error (%s) — serving mock", exc)
        row = None

    if row is None:
        log.info("No InfluxDB data — returning not loaded response")
        return jsonify(NO_DATA_RESPONSE), 503

    def _normalise_level(val) -> str:
        return str(val).strip().title() if val else "Unknown"

    zones = [
        {
            "zone":  ZONE_LABELS[fk],
            "level": _normalise_level(row.get(fk)),
        }
        for fk in ZONE_FIELDS
    ]

    last_seen = row.get("_time")
    log.info(
        "Serving live — people=%s  density=%.0f%%  level=%s",
        row.get("totalPeople"), float(row.get("density", 0)) * 100, row.get("level"),
    )

    return jsonify({
        "live":          True,
        "totalPeople":   int(row.get("totalPeople",   0)),
        "density":       round(float(row.get("density",    0.0)), 2),
        "level":         str(row.get("level",    "UNKNOWN")),
        "trend":         str(row.get("trend",    "UNKNOWN")),
        "bestTime":      str(row.get("bestTime", "UNKNOWN")),
        "score":         round(float(row.get("score",     0.0)), 2),
        "predicted":     round(float(row.get("predicted", 0.0)), 2),
        "prolongedHigh": int(row.get("prolongedHigh", 0)),
        "confidence":    int(row.get("confidence",    0)),
        "temperature":   round(float(row.get("temperature", 0.0)), 1),
        "zones":         zones,
        "recommendation": _recommend(zones) if zones else {},
        "last_updated":  last_seen.isoformat() if last_seen else datetime.now(tz=timezone.utc).isoformat(),
    })

# ─────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────

if __name__ == "__main__":
    log.info("Dashboard API → http://localhost:5001")
    app.run(host="0.0.0.0", port=5001, debug=False)
