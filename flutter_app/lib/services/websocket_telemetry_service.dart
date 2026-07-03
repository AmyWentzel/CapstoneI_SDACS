import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';

import '../config/backend_config.dart';
import '../models/node_telemetry.dart';

class WebSocketTelemetryService {
  WebSocketTelemetryService({
    this.config = const BackendConfig(),
    this.useMockFallback = false,
  });

  final BackendConfig config;
  final bool useMockFallback;
  final StreamController<NodeTelemetry> _controller =
      StreamController<NodeTelemetry>.broadcast();

  WebSocketChannel? _channel;
  StreamSubscription<dynamic>? _subscription;
  Timer? _reconnectTimer;
  bool _closed = false;

  Stream<NodeTelemetry> get telemetryStream => _controller.stream;

  Future<void> connect() async {
    _closed = false;
    _connectSocket();
  }

  Future<void> disconnect() async {
    _closed = true;
    _reconnectTimer?.cancel();
    await _subscription?.cancel();
    await _channel?.sink.close();
    _subscription = null;
    _channel = null;
  }

  void _connectSocket() {
    if (_closed) {
      return;
    }

    try {
      unawaited(_subscription?.cancel());
      _channel = WebSocketChannel.connect(Uri.parse(config.websocketUrl));
      _subscription = _channel!.stream.listen(
        _handleMessage,
        onError: (_) => _scheduleReconnect(),
        onDone: _scheduleReconnect,
        cancelOnError: true,
      );
    } catch (_) {
      _scheduleReconnect();
    }
  }

  void _handleMessage(dynamic message) {
    try {
      final decoded = jsonDecode(message as String);
      if (decoded is Map<String, dynamic> && !_controller.isClosed) {
        _controller.add(NodeTelemetry.fromJson(decoded));
      }
    } catch (_) {
      // Ignore malformed telemetry frames and wait for the next update.
    }
  }

  void _scheduleReconnect() {
    if (_closed || _reconnectTimer?.isActive == true) {
      return;
    }
    _emitMockFallback();
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      _reconnectTimer = null;
      _connectSocket();
    });
  }

  void _emitMockFallback() {
    if (!useMockFallback || _controller.isClosed) {
      return;
    }
    for (final telemetry in NodeTelemetry.mockList()) {
      _controller.add(telemetry);
    }
  }
}
