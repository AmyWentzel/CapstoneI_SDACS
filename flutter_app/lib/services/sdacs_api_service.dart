import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config/backend_config.dart';
import '../models/capture_session.dart';
import '../models/node_telemetry.dart';

class SdacsApiException implements Exception {
  const SdacsApiException(this.message);

  final String message;

  @override
  String toString() => message;
}

class SdacsApiService {
  const SdacsApiService({this.config = const BackendConfig()});

  static const Duration _timeout = Duration(seconds: 5);

  final BackendConfig config;

  Future<bool> checkBackendHealth() async {
    try {
      final json = await _getJson('/api/health');
      return json['status'] == 'ok';
    } on SdacsApiException {
      return false;
    }
  }

  Future<List<NodeTelemetry>> getNodes() async {
    final json = await _getJson('/api/nodes');
    if (json is! List) {
      throw const SdacsApiException('Backend returned invalid node list.');
    }

    return json
        .whereType<Map<String, dynamic>>()
        .map(NodeTelemetry.fromJson)
        .toList()
      ..sort((a, b) => a.nodeId.compareTo(b.nodeId));
  }

  Future<NodeTelemetry> getNode(String nodeId) async {
    final json = await _getJson('/api/nodes/${Uri.encodeComponent(nodeId)}');
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid node data.');
    }
    return NodeTelemetry.fromJson(json);
  }

  Future<NodeTelemetry> getLatestFeatures(String nodeId) async {
    final json = await _getJson(
      '/api/nodes/${Uri.encodeComponent(nodeId)}/features/latest',
    );
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid feature data.');
    }
    return NodeTelemetry.fromJson(json);
  }

  Future<CaptureSession> startCapture({
    required int delayMs,
    required int recordSeconds,
  }) async {
    final json = await _postJson('/api/capture/start', {
      'delay_ms': delayMs,
      'record_seconds': recordSeconds,
    });
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid capture data.');
    }
    return CaptureSession.fromCaptureStartResponse(json);
  }

  Future<Map<String, dynamic>> sendNodeCommand(
    String nodeId,
    String command,
  ) async {
    final json = await _postJson(
      '/api/nodes/${Uri.encodeComponent(nodeId)}/command',
      {'cmd': command},
    );
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid command data.');
    }
    return json;
  }

  Future<dynamic> _getJson(String path) async {
    final uri = Uri.parse('${config.baseUrl}$path');
    try {
      final response = await http.get(uri).timeout(_timeout);
      return _decodeResponse(response);
    } on TimeoutException {
      throw SdacsApiException('Backend request timed out: $uri');
    } on http.ClientException catch (error) {
      throw SdacsApiException('Backend is unavailable: ${error.message}');
    } on FormatException {
      throw SdacsApiException('Backend returned invalid JSON: $uri');
    }
  }

  Future<dynamic> _postJson(String path, Map<String, dynamic> body) async {
    final uri = Uri.parse('${config.baseUrl}$path');
    try {
      final response = await http
          .post(
            uri,
            headers: const {'Content-Type': 'application/json'},
            body: jsonEncode(body),
          )
          .timeout(_timeout);
      return _decodeResponse(response);
    } on TimeoutException {
      throw SdacsApiException('Backend request timed out: $uri');
    } on http.ClientException catch (error) {
      throw SdacsApiException('Backend is unavailable: ${error.message}');
    } on FormatException {
      throw SdacsApiException('Backend returned invalid JSON: $uri');
    }
  }

  dynamic _decodeResponse(http.Response response) {
    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw SdacsApiException(
        'Backend returned HTTP ${response.statusCode}: ${response.body}',
      );
    }
    if (response.body.isEmpty) {
      return null;
    }
    return jsonDecode(response.body);
  }
}
