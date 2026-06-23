# Flutter API Integration

## Branch Purpose

The `feature/flutter-api-backend` branch connects the SDACS telemetry path:

```text
ESP32-S3 MQTT telemetry
-> RPi5 FastAPI backend
-> Flutter app
```

The Flutter app uses the FastAPI backend for REST and WebSocket data. It does not connect directly to MQTT, and it should not depend on Node-RED for normal app operation. Node-RED remains useful as a debugging dashboard.

## Network Assumptions

- ESP32 nodes, the RPi5, the development PC, and any phone running the Flutter app must be on the same LAN.
- ESP32 nodes require 2.4 GHz Wi-Fi.
- The Flutter app should point to the RPi5 backend IP, not `localhost`.
- Example backend IP: `192.168.5.40`.

## Backend Setup

Run the FastAPI backend from the repo root:

```bash
cd backend/sdacs_api
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Windows PowerShell activation:

```powershell
cd backend/sdacs_api
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

By default, the backend connects to MQTT at `192.168.5.40:1883`. Override this with:

```bash
export SDACS_MQTT_HOST=192.168.5.40
export SDACS_MQTT_PORT=1883
```

PowerShell equivalent:

```powershell
$env:SDACS_MQTT_HOST = "192.168.5.40"
$env:SDACS_MQTT_PORT = "1883"
```

## Flutter Run Command

Run Flutter from the app folder and point it at the RPi5 backend:

```bash
cd flutter_app
flutter pub get
flutter run --dart-define=SDACS_BACKEND_IP=192.168.5.40 --dart-define=SDACS_BACKEND_PORT=8000
```

Use the RPi5 LAN IP for `SDACS_BACKEND_IP`. Do not use `localhost` unless the Flutter app and backend are running on the same machine and platform networking supports it.

## API Endpoint Summary

- `GET /api/health` - backend and MQTT connection health.
- `GET /api/nodes` - latest known state for all nodes.
- `GET /api/nodes/{node_id}` - latest known state for one node.
- `GET /api/nodes/{node_id}/features/latest` - latest feature packet for one node.
- `GET /api/captures` - recent capture-complete events held in memory.
- `POST /api/capture/start` - publish a group capture command.
- `POST /api/nodes/{node_id}/command` - publish an allowed node command.
- `WS /ws/sdacs/live` - live normalized telemetry stream for Flutter.

## Example Capture Start Request

Flutter sends this to `POST /api/capture/start`:

```json
{
  "delay_ms": 5000,
  "record_seconds": 20
}
```

The backend publishes this MQTT command:

```json
{
  "cmd": "start_capture",
  "request_id": "capture_YYYYMMDDHHMMSS",
  "delay_ms": 5000,
  "record_seconds": 20
}
```

Published to:

```text
sdacs/group/all/cmd
```

## Testing Checklist

- Mosquitto is running on the RPi5.
- Backend connects to MQTT.
- `GET /api/health` returns `status: ok`.
- Flutter reaches the backend over the LAN using the RPi5 IP.
- MQTT feature packets appear in Flutter.
- Capture button sends a `start_capture` command.
- ESP32 nodes respond to the capture command.
- Capture complete events appear in the backend.
- App handles backend disconnect by showing an error instead of crashing.

Useful quick checks:

```bash
curl http://192.168.5.40:8000/api/health
curl http://192.168.5.40:8000/api/nodes
```

Publish a test feature packet:

```bash
mosquitto_pub -h 192.168.5.40 -t sdacs/node/node01/features -m '{"seq":1,"rms":0.42,"dbfs":-12.5,"db_spl":71.2,"f_peak_hz":997.0}'
```

Start capture through the API:

```bash
curl -X POST http://192.168.5.40:8000/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{"delay_ms":5000,"record_seconds":20}'
```

## Known Limitations

- SPL calibration is still being refined.
- ICS-43432 audio cleanup is separate from this phase.
- Edge Impulse inference is not required for this phase.
- Fuel gauge may still report invalid until the hardware issue is fixed.
- Node-RED remains useful for debugging, but Flutter should use the FastAPI backend.
