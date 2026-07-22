import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/capture_session.dart';

void main() {
  test('backend capture ID and terminal state are preserved', () {
    final session = CaptureSession.fromJson({
      'capture_id': 'capture_20260722T203500123Z',
      'status': 'acoustic_only',
      'requested_at': '2026-07-22T20:35:00Z',
      'scheduled_start_at': '2026-07-22T20:35:05Z',
      'record_seconds': 60,
      'completed_nodes': ['node01', 'node02', 'node03', 'node04'],
      'missing_nodes': <String>[],
      'model_status': 'model_not_configured',
    });

    expect(session.captureId, 'capture_20260722T203500123Z');
    expect(session.durationSeconds, 60);
    expect(session.isTerminal, isTrue);
    expect(session.modelStatus, 'model_not_configured');
  });

  test('pending capture is not terminal', () {
    final session = CaptureSession.fromJson({
      'capture_id': 'capture_pending',
      'status': 'waiting_for_nodes',
      'requested_at': '2026-07-22T20:35:00Z',
      'scheduled_start_at': '2026-07-22T20:35:05Z',
      'record_seconds': 60,
    });
    expect(session.isTerminal, isFalse);
  });
}
