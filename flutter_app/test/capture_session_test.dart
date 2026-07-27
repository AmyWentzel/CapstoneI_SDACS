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

  test('combined result parses capture-specific node metrics', () {
    final result = CaptureCombinedResult({
      'capture_id': 'capture_metrics',
      'acoustic_analysis': {
        'generated_at': '2026-07-27T20:55:59Z',
        'node_metrics': {
          for (var i = 1; i <= 4; i++)
            'node0$i': {
              'node_id': 'node0$i',
              'sample_count': 59,
              'mean_rms': 0.00004 * i,
              'mean_dbfs': -90.0 + i,
              'mean_estimated_spl_db': 30.0 + i,
              'peak_frequency_hz': 100.0 + i,
            },
        },
      },
    });
    expect(result.nodeMetrics.length, 4);
    expect(result.nodeMetrics['node01']!.sampleCount, 59);
    expect(result.nodeMetrics['node04']!.estimatedSplDb, 34);
  });

  test('typed result keeps absent numeric values null', () {
    final result = CaptureCombinedResult({
      'capture_id': 'capture_nullable',
      'acoustic_analysis': {
        'status': 'partial',
        'nodes_used': ['node01'],
        'missing_nodes': ['node02', 'node03', 'node04'],
        'node_metrics': {
          'node01': {'node_id': 'node01'},
        },
      },
      'edge_impulse': {
        'status': 'disabled',
        'confidence': null,
        'scores': <String, dynamic>{},
      },
    });
    final acoustic = result.acousticAnalysis;
    final metric = acoustic.nodeMetrics['node01']!;
    expect(acoustic.meanEstimatedSplDb, isNull);
    expect(acoustic.dominantFrequencyHz, isNull);
    expect(metric.sampleCount, isNull);
    expect(metric.estimatedSplDb, isNull);
    expect(metric.peakFrequencyHz, isNull);
    expect(result.edgeImpulseResult.confidence, isNull);
    expect(result.edgeImpulseResult.isUnavailable, isTrue);
  });

  test('capture failure aliases prefer failure_reason then error_message', () {
    final session = CaptureSession.fromJson({
      'capture_id': 'capture_failed',
      'status': 'failed',
      'requested_at': '2026-07-22T20:35:00Z',
      'scheduled_start_at': '2026-07-22T20:35:05Z',
      'failure_stage': 'capture_data_extraction',
      'failure_reason': 'Specific failure reason',
      'error_message': 'Fallback error message',
      'detail': 'Generic detail',
    });
    expect(session.failureStage, 'capture_data_extraction');
    expect(session.failureReason, 'Specific failure reason');
  });
}
