class CaptureSession {
  const CaptureSession({
    required this.captureId,
    required this.status,
    required this.requestedAt,
    required this.scheduledStartAt,
    required this.durationSeconds,
    required this.completedNodes,
    required this.missingNodes,
    this.processingStage,
    this.modelStatus,
  });

  final String captureId;
  final String status;
  final DateTime requestedAt;
  final DateTime scheduledStartAt;
  final int durationSeconds;
  final List<String> completedNodes;
  final List<String> missingNodes;
  final String? processingStage;
  final String? modelStatus;

  String get sessionId => captureId;
  bool get isTerminal =>
      const {'complete', 'partial', 'failed', 'acoustic_only'}.contains(status);

  factory CaptureSession.fromJson(Map<String, dynamic> json) {
    List<String> strings(String key) =>
        (json[key] as List<dynamic>? ?? const [])
            .map((value) => value.toString())
            .toList();
    final requested = json['requested_at']?.toString();
    final scheduled = json['scheduled_start_at']?.toString();
    return CaptureSession(
      captureId:
          json['capture_id']?.toString() ??
          json['request_id']?.toString() ??
          'unknown',
      status: json['status']?.toString() ?? 'requested',
      requestedAt: DateTime.tryParse(requested ?? '') ?? DateTime.now(),
      scheduledStartAt: DateTime.tryParse(scheduled ?? '') ?? DateTime.now(),
      durationSeconds: (json['record_seconds'] as num?)?.toInt() ?? 0,
      completedNodes: strings('completed_nodes'),
      missingNodes: strings('missing_nodes'),
      processingStage: json['processing_stage']?.toString(),
      modelStatus: json['model_status']?.toString(),
    );
  }

  factory CaptureSession.fromCaptureStartResponse(Map<String, dynamic> json) =>
      CaptureSession.fromJson(json);
}

class CaptureCombinedResult {
  const CaptureCombinedResult(this.json);
  final Map<String, dynamic> json;
  String get captureId => json['capture_id']?.toString() ?? 'unknown';
  Map<String, dynamic> get acoustic => _map('acoustic_analysis');
  Map<String, dynamic> get edgeImpulse => _map('edge_impulse');
  Map<String, dynamic> get fusion => _map('fusion');
  Map<String, dynamic> get recommendation => _map('recommendation');
  Map<String, dynamic> _map(String key) =>
      (json[key] as Map?)?.map(
        (key, value) => MapEntry(key.toString(), value),
      ) ??
      {};
}
