# SDACS Flutter App

Flutter client for the Smart Distributed Acoustic Calibration System.

This app uses the SDACS FastAPI backend for REST and WebSocket telemetry. It does not connect directly to MQTT or Node-RED during normal operation.

Current broker API: `http://192.168.5.61:8000`

Run against the current broker:

```powershell
flutter run --dart-define=SDACS_BACKEND_IP=192.168.5.61 --dart-define=SDACS_BACKEND_PORT=8000
```

Current milestone: Flutter REST telemetry proof only. OTA, BLE, ESP flashing, and firmware changes are handled separately.

See [Flutter API Integration](../docs/flutter_api_integration.md) for backend setup, run commands, endpoint summaries, and the repeatable test checklist.

## Getting Started

This project is a starting point for a Flutter application.

A few resources to get you started if this is your first Flutter project:

- [Learn Flutter](https://docs.flutter.dev/get-started/learn-flutter)
- [Write your first Flutter app](https://docs.flutter.dev/get-started/codelab)
- [Flutter learning resources](https://docs.flutter.dev/reference/learning-resources)

For help getting started with Flutter development, view the
[online documentation](https://docs.flutter.dev/), which offers tutorials,
samples, guidance on mobile development, and a full API reference.
