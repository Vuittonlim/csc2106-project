// ─────────────────────────────────────────────────────────────────────────────
// Flux queries for crowd monitoring — InfluxDB
// Run these in the InfluxDB UI Data Explorer or call via the Python client.
// ─────────────────────────────────────────────────────────────────────────────


// ── 1. Mean end-to-end latency per protocol path (last 1 hour) ────────────────
//    Answers: BLE vs HTTP vs MQTT — which is fastest in your deployment?

from(bucket: "crowd_monitoring")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "latency_ms")
  |> group(columns: ["src"])
  |> mean()
  |> rename(columns: {_value: "mean_latency_ms"})


// ── 2. Latency percentiles per protocol (last 6 hours) ───────────────────────
//    p50 / p95 tells you typical vs worst-case — more useful than mean alone.

from(bucket: "crowd_monitoring")
  |> range(start: -6h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "latency_ms")
  |> group(columns: ["src"])
  |> quantile(q: 0.95)
  |> rename(columns: {_value: "p95_latency_ms"})


// ── 3. Fallback rate per zone (last 24 hours) ────────────────────────────────
//    High fallback rate = BLE or ESP32 gateway is unstable for that zone.

from(bucket: "crowd_monitoring")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "latency_ms")
  |> group(columns: ["zone", "fallback"])
  |> count()
  |> rename(columns: {_value: "message_count"})


// ── 4. Crowd level over time for a specific zone (rolling 15-min window) ──────
//    Used by the prediction engine as input signal.

from(bucket: "crowd_monitoring")
  |> range(start: -2h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor"
                    and r._field == "c_level"
                    and r.zone == "seating_1")
  |> aggregateWindow(every: 5m, fn: mean, createEmpty: false)
  |> yield(name: "crowd_trend")


// ── 5. All zones side-by-side — latest crowd level ───────────────────────────
//    Dashboard snapshot: one row per zone showing current c_level.

from(bucket: "crowd_monitoring")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "c_level")
  |> group(columns: ["zone"])
  |> last()
  |> keep(columns: ["zone", "_value", "_time"])


// ── 6. Environmental context — temperature & humidity per zone (last 30 min) ──
//    Feed into fusion confidence: high humidity may affect DHT22 accuracy.

from(bucket: "crowd_monitoring")
  |> range(start: -30m)
  |> filter(fn: (r) => r._measurement == "crowd_sensor"
                    and (r._field == "t" or r._field == "h"))
  |> group(columns: ["zone", "_field"])
  |> mean()


// ── 7. PIR vs sound agreement rate ───────────────────────────────────────────
//    If PIR=1 but s_level=0 often, your sound threshold may need tuning.
//    (Join the two field streams and compare.)

pir = from(bucket: "crowd_monitoring")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "pir")
  |> aggregateWindow(every: 1m, fn: mean, createEmpty: false)

sound = from(bucket: "crowd_monitoring")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "s_level")
  |> aggregateWindow(every: 1m, fn: mean, createEmpty: false)

join(tables: {pir: pir, sound: sound}, on: ["_time", "zone"])
  |> map(fn: (r) => ({r with agreement: if r._value_pir > 0.5 and r._value_sound > 0.5 then 1 else 0}))
  |> mean(column: "agreement")


// ── 8. Hourly occupancy heatmap (last 7 days) ────────────────────────────────
//    Feeds the prediction / recommendation engine for time-of-day patterns.

from(bucket: "crowd_monitoring")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "c_level")
  |> group(columns: ["zone"])
  |> aggregateWindow(every: 1h, fn: mean, createEmpty: false)
  |> yield(name: "hourly_heatmap")


// ── 9. Battery health — M5StickC devices below 20% ───────────────────────────

from(bucket: "crowd_monitoring")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "crowd_sensor" and r._field == "b")
  |> filter(fn: (r) => r._value >= 0 and r._value < 20)  // -1 = absent, skip
  |> group(columns: ["zone"])
  |> last()
  |> keep(columns: ["zone", "_value", "_time"])
