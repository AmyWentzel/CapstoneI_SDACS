import '../models/capture_session.dart';

class SdacsApiService {
  const SdacsApiService();

  Future<bool> checkBackendHealth() async {
    await Future<void>.delayed(const Duration(milliseconds: 500));
    return true;
  }

  Future<CaptureSession> startTestCapture() async {
    await Future<void>.delayed(const Duration(milliseconds: 400));
    return CaptureSession(
      sessionId: 'test-${DateTime.now().millisecondsSinceEpoch}',
      status: 'running',
      startedAt: DateTime.now(),
      durationSeconds: 30,
      delayMs: 0,
      nodeIds: const ['node01', 'node02', 'node03', 'node04'],
    );
  }

  Future<void> stopTestCapture() async {
    await Future<void>.delayed(const Duration(milliseconds: 300));
  }
}
