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
}
