| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## SDACS BLE Node Awareness

On boot, each ESP32-S3 node loads config/NVS defaults, then runs a short BLE
node-awareness advertisement before Wi-Fi and MQTT start. The BLE local name is
`SDACS-<node_id>`, for example `SDACS-node01`, and the advertisement is
non-connectable. The RPi5 gateway scans these advertisements and RSSI values,
then publishes topology updates such as `sdacs/site/topology` and node presence
topics over MQTT.

After `SDACS_BLE_LOCATOR_DURATION_MS`, the node stops advertising, shuts down
NimBLE, and starts the normal SDACS Wi-Fi/MQTT sensing path. The node continues
to publish heartbeat, battery, temp/humidity, audio, and feature topics under
`sdacs/node/<node_id>/...`, and subscribes to its node command topic plus
`sdacs/group/all/cmd`.

Node-RED can trigger `scan_now` on the RPi5 gateway. The ESP32 nodes do not scan
for BLE devices and do not receive commands over BLE.

For reliable dashboard Scan Now behavior, import
`node_red/sdacs_flow_fixed.json`. Its Scan Now button first publishes:

```json
{"cmd":"ble_advertise","request_id":"ble_scan_YYYYMMDDHHMMSS","duration_ms":15000}
```

to `sdacs/group/all/cmd`, waits about 1 second, then publishes the Raspberry Pi
gateway scan command to `sdacs/ble/gateway/rpi5/cmd`:

```json
{"command":"scan_now","scan_seconds":15,"source":"node-red"}
```

Each node advertises as `SDACS-<node_id>` with manufacturer payload
`SDACS:<node_id>` during the requested window.

## SDACS Hardware Pinout

- ICS-43432 I2S microphone: BCLK GPIO11, DOUT/DIN GPIO12, LRCLK/WS GPIO13.
- SHT41 temperature/humidity sensor: I2C SDA GPIO47, SCL GPIO48, address `0x44`.
- MAX17048 fuel gauge: shared I2C bus, address `0x36`.
- Battery LED array: GPIO14, GPIO15, GPIO16, GPIO17, GPIO18, GPIO1.
- Metro ESP32-S3 microSD: SCK GPIO39, MOSI GPIO42, MISO GPIO21, CS GPIO45.

## Flashing Four Unique Nodes

Node identity is compiled into the firmware through `SDACS_SECRET_NODE_ID`.
Runtime NVS `node_id` values are deprecated and ignored. Use one firmware build
per node so BLE, MQTT, storage, telemetry JSON, OTA status, and future
API/Flutter integrations all use the same fixed identity.

```powershell
.\flash_metro.ps1 -Port COM7  -NodeId node01 -Erase
.\flash_metro.ps1 -Port COM9  -NodeId node02 -Erase
.\flash_metro.ps1 -Port COM10 -NodeId node03 -Erase
.\flash_metro.ps1 -Port COM11 -NodeId node04 -Erase
```

Expected BLE names:

```text
SDACS-node01
SDACS-node02
SDACS-node03
SDACS-node04
```

Subscribe Node-RED or a test client to all node topics with:

```text
sdacs/node/+/#
```

On boot, the serial monitor should show the compiled and active node identity,
the BLE name, and the MQTT base topic, for example:

```text
Compiled Node ID: node02
Active Node ID: node02
BLE name: SDACS-node02
MQTT base: sdacs/node/node02
```

Troubleshooting:

- If BLE still shows `node01`, erase flash once with `-Erase`.
- Confirm the serial monitor shows the expected compiled and active Node ID.
- Confirm the firmware is not reading `node_id` from NVS.
- Confirm Node-RED subscribes to wildcard topics such as `sdacs/node/+/#`.

## Delayed Synchronized Capture

Branch: `feature/delayed-synched-capture`

After BLE discovery and Wi-Fi/MQTT startup, the node remains idle until MQTT
receives a `start_capture` command on either `sdacs/node/<node_id>/cmd` or
`sdacs/group/all/cmd`.

Example Node-RED group command:

```json
{
  "cmd": "start_capture",
  "request_id": "capture_001",
  "delay_ms": 5000,
  "record_seconds": 20
}
```

Accepted commands publish capture status on `sdacs/node/<node_id>/status`, then
the node waits `delay_ms`, records to SD, finalizes raw/WAV/metrics files,
verifies they are non-empty, and only then publishes
`sdacs/node/<node_id>/capture_complete`. Audio chunks are not streamed over
MQTT; compact feature metrics are published during capture on
`sdacs/node/<node_id>/features`.

Feature telemetry uses compact summary JSON only; raw audio samples are not sent
over MQTT. The feature `err` field is the compact sum of the latest SHT41
temp/humidity and MAX17048 fuel gauge sample error counters at the time the
feature record was built. Serial logs include `Audio feature debug` lines during
capture with raw I2S word 0, converted sample 0, min/max, `p2p_raw`, `zeros`,
`rms`, `dbfs`, `db_spl`, `f_peak_hz`, and `sample_count`.

ICS-43432 SPL conversion uses `SDACS_CAL_OFFSET_DB = 120.0f`, derived from the
microphone sensitivity of `-26 dBFS @ 94 dB SPL` (`94 - (-26) = 120`). The I2S
configuration is Philips-format with stereo I2S frame timing while reading the
selected mono mic slot. If hardware debug shows zeros or a pinned signal, the
next controlled test is changing `SDACS_I2S_USE_RIGHT_SLOT` to `1`.

Fuel gauge is still sampled periodically for LEDs and battery state, but MQTT
`fuel_gauge` records are only published on `capture_start`, `capture_complete`,
and `report_status`, so CSV storage is not dominated by battery rows.

## MQTT Telemetry Verification

Use these subscriptions while booting a node and running a group capture:

```bash
mosquitto_sub -h 192.168.5.40 -t 'sdacs/node/+/features' -v
mosquitto_sub -h 192.168.5.40 -t 'sdacs/node/+/temp_humidity' -v
mosquitto_sub -h 192.168.5.40 -t 'sdacs/node/+/fuel_gauge' -v
mosquitto_sub -h 192.168.5.40 -t 'sdacs/node/+/status/heartbeat' -v
mosquitto_sub -h 192.168.5.40 -t 'sdacs/node/+/capture_complete' -v
```

Expected record types include `features`, `temp_humidity`, `fuel_gauge`,
`heartbeat`, and `capture_complete`.

## SD Storage Diagnostics

Heartbeat records include `storage_mounted`, `storage_last_error`,
`storage_error_detail`, and `sd_mount_attempts`. To validate SD behavior:

- Boot with SD inserted and confirm `storage_mounted=true` in heartbeat/status.
- Send `{"cmd":"storage_status","request_id":"storage_001"}`.
- Start a 10 s capture and confirm raw/WAV/CSV files are created.
- Boot without SD inserted and confirm Wi-Fi/MQTT still come online.
- Confirm capture is rejected with `SD storage not mounted`.
- Insert or fix the SD card and send `{"cmd":"storage_remount","request_id":"storage_remount_001"}` while idle.

Lab OTA note: the current sdkconfig keeps HTTP OTA disabled
(`# CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP is not set`). Use HTTPS OTA URLs unless a
separate lab-mode config intentionally enables HTTP.

The fixed Node-RED export adds an inject-on-deploy path that overwrites
`/home/vortex/sdacs_logs/sdacs_telemetry.csv` with the CSV header before append
rows begin. The download endpoint remains `/sdacs/download/csv`.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.
