# SDACS API Backend

FastAPI bridge between SDACS ESP32-S3 MQTT telemetry and the Flutter app.

## Configuration

Defaults:

- MQTT broker: `192.168.5.40:1883`
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

This phase intentionally keeps state in memory and does not add SQLite.

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
