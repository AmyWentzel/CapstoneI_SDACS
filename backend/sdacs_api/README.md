# SDACS API Backend

FastAPI bridge between SDACS ESP32-S3 MQTT telemetry and the Flutter app.

## Configuration

This packaged application is the authoritative production backend. Do not run a
second flat FastAPI application.

Defaults:

- MQTT broker: `127.0.0.1:1883`
- API bind: `0.0.0.0:8000`

Environment overrides:

```powershell
$env:SDACS_MQTT_HOST="192.168.5.40"
$env:SDACS_MQTT_PORT="1883"
$env:SDACS_API_HOST="0.0.0.0"
$env:SDACS_API_PORT="8000"
```

## Run

From `backend/sdacs_api`:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python -m uvicorn app.main:app --host $env:SDACS_API_HOST --port $env:SDACS_API_PORT
```

If the host or port variables are not set, use:

```powershell
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

## MQTT Subscriptions

- `sdacs/node/+/features`
- `sdacs/node/+/status/heartbeat`
- `sdacs/node/+/status`
- `sdacs/node/+/capture_complete`
- `sdacs/node/+/temp_humidity`
- `sdacs/node/+/fuel_gauge`
- `sdacs/node/+/ota/status`
- `sdacs/node/+/presence`
- `sdacs/node/+/acoustic_ai`

Incoming JSON payloads are normalized into a common telemetry object and stored in memory.

## REST API

- `GET /api/health`
- `GET /api/nodes`
- `GET /api/nodes/{node_id}`
- `GET /api/nodes/{node_id}/features/latest`
- `GET /api/captures`
- `POST /api/capture/start`
- `POST /api/nodes/{node_id}/command`

`POST /api/capture/start` publishes to `sdacs/group/all/cmd`:

```json
{
  "cmd": "start_capture",
  "request_id": "capture_YYYYMMDDHHMMSS",
  "delay_ms": 5000,
  "record_seconds": 20
}
```

`POST /api/nodes/{node_id}/command` publishes to `sdacs/node/<node_id>/cmd`.
Allowed commands are `report_status` and `reboot`.

## WebSocket

- `/ws/sdacs/live`

Each normalized MQTT telemetry update is broadcast as JSON to connected clients.

## Quick Test

Health:

```powershell
Invoke-RestMethod http://localhost:8000/api/health
```

Publish a feature packet:

```powershell
mosquitto_pub -h 192.168.5.40 -t sdacs/node/node01/features -m "{\"seq\":1,\"rms\":123.4,\"dbfs\":-18.2,\"f_peak_hz\":1000}"
```

Start capture:

```powershell
Invoke-RestMethod -Method Post http://localhost:8000/api/capture/start `
  -ContentType "application/json" `
  -Body '{"delay_ms":5000,"record_seconds":20}'
```

Live node state remains in memory. Capture sessions and capture-associated
telemetry are persisted with `CREATE TABLE IF NOT EXISTS` migrations in the
existing SQLite database configured by `SDACS_SQLITE_PATH`.

## Capture processing and Edge Impulse

Normal `POST /api/capture/start` requests are unlabelled. The returned
`capture_id` is preserved through MQTT, node completion tracking, acoustic
artifacts, model status, fusion, and Flutter results. An optional
`validation_label` is available for controlled validation only and is never a
model feature.

Capture artifacts are stored below `SDACS_CAPTURE_DATA_DIR` as:

```text
captures/<capture_id>/
  manifest.json
  acoustic_input.csv
  acoustic_summary.json
  acoustic_map.png
  edge_impulse_input.csv
  edge_impulse_schema.json
  edge_impulse_result.json
  combined_result.json
  processor_stdout.log
  processor_stderr.log
```

The active acoustic processor is `python_sdacs_acoustic_map`. Until the final
model and feature schema are available, keep `SDACS_EI_ENABLED=false`. The
production adapter returns an explicit unavailable status and never fabricates
predictions or confidence values.

## Raspberry Pi service

Authoritative application import and service configuration:

```ini
[Service]
WorkingDirectory=/home/kyledavid36/sdacs/backend/sdacs_api
Environment=SDACS_MQTT_HOST=127.0.0.1
Environment=SDACS_MQTT_PORT=1883
Environment=SDACS_EI_ENABLED=false
Environment=SDACS_CAPTURE_DATA_DIR=/home/kyledavid36/sdacs/captures
Environment=SDACS_SQLITE_PATH=/home/vortex/sdacs_logs/sdacs_telemetry.db
ExecStart=/home/kyledavid36/sdacs/backend/venv/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Deploy source without generated environments or caches:

```bash
rsync -av --delete \
  --exclude='.git/' --exclude='venv/' --exclude='.venv/' \
  --exclude='__pycache__/' --exclude='*.pyc' --exclude='build/' \
  --exclude='.dart_tool/' \
  ./ kyledavid36@raspberrypi:/home/kyledavid36/sdacs/

/home/kyledavid36/sdacs/backend/venv/bin/python -m pip install \
  -r /home/kyledavid36/sdacs/backend/sdacs_api/requirements.txt
sudo systemctl daemon-reload
sudo systemctl restart sdacs-api
sudo systemctl status sdacs-api --no-pager
```

When the final model is ready, set both:

```ini
Environment=SDACS_EI_ENABLED=true
Environment=SDACS_EIM_PATH=/home/kyledavid36/sdacs/models/<final-model>.eim
```

Do not enable inference until that file and its exact schema have been validated.

## Raspberry Pi BLE scanning

`POST /api/ble/scan` must run on the Raspberry Pi (or another host that
physically owns the scanning Bluetooth adapter). Flutter never accesses
Bluetooth hardware directly.

```bash
sudo systemctl enable --now bluetooth
systemctl status bluetooth --no-pager
rfkill list bluetooth
bluetoothctl list
python3 -m pip install -r requirements.txt
sudo systemctl restart sdacs-api
curl -X POST http://127.0.0.1:8000/api/ble/scan
```

If `rfkill` reports a blocked adapter, run `sudo rfkill unblock bluetooth`.
FastAPI itself does not invoke `sudo`; its service account must be able to use
BlueZ over the system D-Bus. Adapter, BlueZ/D-Bus, permission, timeout, and MQTT
publish failures are returned as HTTP errors rather than empty successful scans.

The endpoint publishes `ble_advertise` to `sdacs/group/all/cmd`, waits one
second, scans for eight seconds, and returns only detected `SDACS-node01`
through `SDACS-node04` devices with median `ble_rssi_dbm` values.

## SPL calibration API

The backend now supports an end-to-end four-node SPL calibration workflow:

- `POST /api/calibration/preview` calculates safe per-node proposals from a completed `calibration_1khz` or `spl_calibration_verification` capture.
- `POST /api/calibration/apply` sends `set_cal_offset` to each node and requires a matching firmware acknowledgement.
- `GET /api/calibration/latest` returns the latest persisted apply record.
- `GET /api/captures/{capture_id}/calibration` returns the capture-specific `calibration_result.json` artifact.

See `../../docs/SPL_CALIBRATION_WORKFLOW.md` for the operator procedure and payload examples.
