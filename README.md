Seating Zone 2

This branch contains all code, tests, and experiments for the Seating Zone 2 sensor node in the Canteen Crowd Monitoring system. The final system uses an M5StickC Plus for sensing (microphone + BLE) and a Raspberry Pi Pico W as an MQTT bridge to HiveMQ Cloud.

finalversion/          ← Production code (what's deployed)
├── m5stick/
│   ├── mic_ble_crowdindex.ino
│   └── ble_handler.h
└── picow/
    └── main.py

tests/                 ← All previous iterations and experiments
├── v1-mic-test/           Microphone-only test with public MQTT
├── v2-mic-ble-mqtt/       Added BLE scan + crowd index fusion
├── v3-latency/            Protocol latency benchmarks (WiFi, MQTT, BLE)
├── v4-single-device/      All-in-one M5StickC Plus (TLS + BLE — failed)
│   └── ble_gateway/       BLE gateway with automatic fallback
└── tools/
    └── verify_mqtt.py     Laptop script to verify cloud MQTT messages

v1 — Microphone Test (tests/v1-mic-test/)
Basic proof of concept. M5StickC Plus reads microphone, classifies sound as Low/Medium/High using peak amplitude, and publishes to a free public MQTT broker

v2 — Mic + BLE + Crowd Index (tests/v2-mic-ble-mqtt/)
Added BLE scanning to count nearby devices. Implemented a crowd index algorithm that fuses sound level and BLE device count into a single Low/Medium/High classification. Both values published to public MQTT broker. This version confirmed that combining two sensor inputs gives a more reliable crowd estimate than sound alone. 

v3 — Protocol Latency Benchmarks (tests/v3-latency/)
Wrote a dedicated benchmarking sketch to measure WiFi connection time, MQTT publish latency, BLE scan duration vs devices found, BLE advertising start/stop latency, and protocol startup overhead (BLE init vs WiFi connect). Ran each test multiple times and averaged results

v4 — Single-Device with HiveMQ Cloud TLS (tests/v4-single-device/)
Refactored the code into modular header files (ble_handler.h, mqtt_handler.h, http_handler.h) and switched from the public broker to HiveMQ Cloud (TLS, port 8883) with username/password authentication. This is where the system broke, the M5StickC Plus could not maintain a stable TLS/MQTT connection while running BLE scans. The ESP32's shared 2.4 GHz radio uses time-division multiplexing between WiFi and BLE, and the TLS handshake requires sustained uninterrupted WiFi communication. Every BLE scan interrupted the radio, causing TLS timeouts and MQTT disconnections.
Attempted fixes included removing the HTTP server to free heap memory, increasing TLS timeout, reducing BLE scan frequency. Could not resvole the issue 

finalversion — Two-Device Architecture
Resolved the radio co-existence conflict by splitting responsibilities across two microcontrollers. The M5StickC Plus handles all BLE and sensing (no WiFi), while the Pico W handles all WiFi and MQTT (no BLE). They communicate over a UART serial link at 9600 baud (M5 G26 TX → Pico W GP5 RX, shared GND).