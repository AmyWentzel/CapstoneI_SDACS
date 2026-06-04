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
.\flash_metro.ps1 -Port COM8  -NodeId node01 -Erase
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
`sdacs/node/<node_id>/capture_complete`. Audio chunks and capture feature data
are not streamed live during capture; Node-RED receives completion only after SD
finalization succeeds.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.
