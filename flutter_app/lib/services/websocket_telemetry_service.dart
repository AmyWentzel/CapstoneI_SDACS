import 'dart:async';

import '../models/node_telemetry.dart';

class WebSocketTelemetryService {
  WebSocketTelemetryService();

  final StreamController<NodeTelemetry> _controller =
      StreamController<NodeTelemetry>.broadcast();

  Stream<NodeTelemetry> get telemetryStream => _controller.stream;

  Future<void> connect() async {
    // TODO: Connect to BackendConfig.websocketUrl when Node-RED is ready.
    await Future<void>.delayed(const Duration(milliseconds: 300));
    for (final telemetry in NodeTelemetry.mockList()) {
      if (!_controller.isClosed) {
        _controller.add(telemetry);
      }
    }
  }

  Future<void> disconnect() async {
    await _controller.close();
  }
}
