class CaptureSession {
  const CaptureSession({
    required this.sessionId,
    required this.status,
    required this.startedAt,
    required this.durationSeconds,
    required this.delayMs,
    required this.nodeIds,
  });

  final String sessionId;
  final String status;
  final DateTime startedAt;
  final int durationSeconds;
  final int delayMs;
  final List<String> nodeIds;

  factory CaptureSession.fromCaptureStartResponse(Map<String, dynamic> json) {
    final payload = json['payload'] is Map<String, dynamic>
        ? json['payload'] as Map<String, dynamic>
        : <String, dynamic>{};

    return CaptureSession(
      sessionId: payload['request_id'] as String? ?? 'unknown',
      status: (json['published'] as bool? ?? false) ? 'published' : 'failed',
      startedAt: DateTime.now(),
      durationSeconds: (payload['record_seconds'] as num?)?.toInt() ?? 0,
      delayMs: (payload['delay_ms'] as num?)?.toInt() ?? 0,
      nodeIds: const [],
    );
  }
}
