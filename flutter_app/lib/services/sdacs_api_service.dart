import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config/backend_config.dart';
import '../models/ble_scan_result.dart';
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
  static const Duration _bleScanTimeout = Duration(seconds: 20);

  final BackendConfig config;

  Future<bool> checkBackendHealth() async {
    for (final path in const ['/health', '/api/health']) {
      try {
        final json = await _getJson(path);
        if (json is Map<String, dynamic>) {
          final status = json['status']?.toString().toLowerCase();
          return status == null || status == 'ok' || status == 'healthy';
        }
        return true;
      } on SdacsApiException {
        continue;
      }
    }
    return false;
  }

  Future<List<NodeTelemetry>> getNodes() async {
    final json = await _getJson('/api/nodes');
    final nodeRows = _nodeRows(json);
    if (nodeRows == null) {
      throw const SdacsApiException('Backend returned invalid node list.');
    }

    final nodes = await Future.wait(nodeRows.map(_nodeFromDirectoryRow));
    return nodes..sort((a, b) => a.nodeId.compareTo(b.nodeId));
  }

  Future<NodeTelemetry> getNode(String nodeId) async {
    final encodedNodeId = Uri.encodeComponent(nodeId);
    dynamic json;
    try {
      json = await _getJson('/api/nodes/$encodedNodeId/latest');
    } on SdacsApiException {
      json = await _getJson('/api/nodes/$encodedNodeId');
    }

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

  List<Map<String, dynamic>>? _nodeRows(dynamic json) {
    final rows = json is Map<String, dynamic> ? json['nodes'] : json;
    if (rows is! List) {
      return null;
    }

    return rows
        .whereType<Map>()
        .map((row) => row.map((key, value) => MapEntry(key.toString(), value)))
        .toList();
  }

  Future<NodeTelemetry> _nodeFromDirectoryRow(Map<String, dynamic> row) async {
    final nodeId = _nodeIdFrom(row);
    if (nodeId == null) {
      return NodeTelemetry.fromJson(row);
    }

    try {
      final latest = await _getJson(
        '/api/nodes/${Uri.encodeComponent(nodeId)}/features/latest',
      );
      if (latest is Map<String, dynamic>) {
        return NodeTelemetry.fromJson({...row, 'latest_features': latest});
      }
    } on SdacsApiException {
      // Keep sparse node rows visible even when one latest-feature lookup fails.
    }

    return NodeTelemetry.fromJson(row);
  }

  String? _nodeIdFrom(Map<String, dynamic> json) {
    final value = json['node_id'] ?? json['nodeId'];
    if (value == null) {
      return null;
    }
    final nodeId = value.toString().trim();
    return nodeId.isEmpty ? null : nodeId;
  }

  Future<CaptureSession> startCapture({
    required int delayMs,
    required int recordSeconds,
    String? validationLabel,
    String? requestId,
  }) async {
    final json = await _postJson('/api/capture/start', {
      'delay_ms': delayMs,
      'record_seconds': recordSeconds,
      'validation_label': ?validationLabel,
      'request_id': ?requestId,
    });
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid capture data.');
    }
    return CaptureSession.fromCaptureStartResponse(json);
  }

  Future<CaptureSession> getCapture(String captureId) async {
    final json = await _getJson(
      '/api/captures/${Uri.encodeComponent(captureId)}',
    );
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid capture status.');
    }
    return CaptureSession.fromJson(json);
  }

  Future<CaptureCombinedResult?> getCaptureResult(String captureId) async {
    final uri = Uri.parse(
      '${config.baseUrl}/api/captures/${Uri.encodeComponent(captureId)}/result',
    );
    try {
      final response = await http.get(uri).timeout(_timeout);
      if (response.statusCode == 202) return null;
      final json = _decodeResponse(response);
      if (json is! Map<String, dynamic>) {
        throw const SdacsApiException(
          'Backend returned invalid capture result.',
        );
      }
      return CaptureCombinedResult(json);
    } on TimeoutException {
      throw SdacsApiException('Backend request timed out: $uri');
    } on http.ClientException catch (error) {
      throw SdacsApiException('Backend is unavailable: ${error.message}');
    }
  }

  Uri capturePlotUri(String captureId) => Uri.parse(
    '${config.baseUrl}/api/captures/${Uri.encodeComponent(captureId)}/plot',
  ).replace(queryParameters: {'capture_id': captureId});

  Future<BleScanResult> scanBleNodes() async {
    final json = await _postJson(
      '/api/ble/scan',
      const {},
      timeout: _bleScanTimeout,
    );
    if (json is! Map<String, dynamic>) {
      throw const SdacsApiException('Backend returned invalid BLE scan data.');
    }
    try {
      return BleScanResult.fromJson(json);
    } on FormatException catch (error) {
      throw SdacsApiException(
        'Backend returned invalid BLE scan data: ${error.message}',
      );
    } on TypeError {
      throw const SdacsApiException('Backend returned invalid BLE scan data.');
    }
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

  Future<dynamic> _postJson(
    String path,
    Map<String, dynamic> body, {
    Duration timeout = _timeout,
  }) async {
    final uri = Uri.parse('${config.baseUrl}$path');
    try {
      final response = await http
          .post(
            uri,
            headers: const {'Content-Type': 'application/json'},
            body: jsonEncode(body),
          )
          .timeout(timeout);
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
      var detail = response.body;
      try {
        final decoded = jsonDecode(response.body);
        if (decoded is Map && decoded['detail'] != null) {
          detail = decoded['detail'].toString();
        }
      } on FormatException {
        // Preserve a non-JSON backend error body.
      }
      throw SdacsApiException(
        'Backend returned HTTP ${response.statusCode}: $detail',
      );
    }
    if (response.body.isEmpty) {
      return null;
    }
    return jsonDecode(response.body);
  }
}
