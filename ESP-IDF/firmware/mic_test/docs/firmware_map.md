# Firmware Map (`mic_test`)

## Entry point
- `app_main()` in `main/mic_test.c` is the firmware entry point.
- Startup sequence:
  1. `config_store_init()`
  2. `maybe_provision_network_config()` (one-time provisioning if WiFi SSID is empty)
3. `wifi_mqtt_start()`
  4. `i2s_mic_init()`
  5. Create `mic_test` task

## Tasks created
- `mic_test` (created in `app_main`)
  - API: `xTaskCreatePinnedToCore`
  - Stack: `8192`
  - Priority: `5`
  - Core: `1`
  - Role: reads mic samples over I2S, converts to signed 24-bit samples, computes rolling RMS/db logs, chunks audio, publishes audio chunks to MQTT.

- `mqtt_publish_task` (created in `wifi_mqtt_start`)
  - API: `xTaskCreatePinnedToCore`
  - Stack: `4096`
  - Priority: `5`
  - Core: `0`
  - Role: waits for WiFi, starts MQTT client, drains feature queue (`s_feat_q`), builds JSON, publishes feature messages.

- Also present indirectly: ESP-IDF WiFi/MQTT internal tasks (created by SDK internals, not explicitly named in this code).

## Data flow
- Audio path:
  1. ICS-43434 mic -> I2S RX (`i2s_channel_read`, 32-bit slot words)
  2. `i2s_word_to_s24()` converts each word to signed 24-bit sample
  3. Samples appended into `chunk[AUDIO_CHUNK_SAMPLES]` (`2048` samples)
  4. On full chunk, build binary payload = `sdacs_audio_hdr_t` + sample array
  5. Publish to MQTT topic `<base_topic>/audio` via `wifi_mqtt_publish_raw(...)`

- Feature path (available API, currently optional from mic task):
  1. Producer calls `wifi_mqtt_try_send(sdacs_features_t*)`
  2. Message enters queue `s_feat_q` (len `8`)
  3. `mqtt_publish_task` dequeues, converts to JSON (`build_features_json`)
  4. Publish to base topic `s_cfg.topic`

## MQTT topics and payloads
- Base topic (configurable): `mqtt_topic` from `config_store`.
  - Default: `sdacs/node/node01/features`
  - Provisioning constant in `mic_test.c`: `sdacs/node/node01/features`

- Audio topic:
  - Pattern: `<base_topic>/audio`
  - Example: `sdacs/node/node01/features/audio`
  - Payload: binary
    - Header (`sdacs_audio_hdr_t`, packed):
      - `magic` (`0x43414453`, "SDAC")
      - `ver` (`1`)
      - `flags`
      - `seq` (chunk counter)
      - `t_us` (timestamp from `esp_timer_get_time()`)
      - `sample_rate` (`48000` currently)
      - `n` (samples in this chunk)
    - Body: `n` samples of `int32_t` containing sign-extended 24-bit audio

- Feature topic:
  - Topic: `<base_topic>`
  - Payload: JSON object with fields:
    - `node`, `seq`, `t_us`, `n`, `rms`, `dbfs`, `db_spl`, `f_peak_hz`, `p2p_raw`, `zeros`

## GPIO / pin assumptions
- Mic wiring (current code in `main/mic_test.c`):
  - `BCLK` -> `GPIO16`
  - `WS/LRCLK` -> `GPIO15`
  - `DIN` (mic DOUT to ESP input) -> `GPIO14`
  - `SEL` tied to GND (left channel)
  - Mic power: `3.3V` and `GND`

## Configuration vs runtime state
- Configuration (persistent, NVS-backed via `config_store`):
  - WiFi: `wifi_ssid`, `wifi_pass`
  - MQTT: `mqtt_uri`, `mqtt_topic`
  - Node/device: `node_id`
  - Audio/calibration params: `sample_hz`, `cal_mdb` (calibration offset in milli-dB)
  - One-time compile-time provisioning constants in `mic_test.c` can seed WiFi/MQTT config when WiFi SSID is empty.

- Runtime state (RAM, rebuilt each boot):
  - Handles and flags: `rx_chan`, `s_wifi_event_group`, `s_feat_q`, `s_mqtt`, `s_mqtt_connected`, retry counters
  - Buffers: `raw[]`, `chunk[]`, `payload[]`
  - Task-local counters/timing: sample counters, RMS accumulation, chunk sequence/timestamps
  - Derived topic string: `<base_topic>/audio`
