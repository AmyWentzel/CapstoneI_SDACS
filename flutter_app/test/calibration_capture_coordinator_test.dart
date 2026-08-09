// SDACS Flutter verification: regression coverage for calibration capture coordinator test.
// These tests protect operator-visible behavior during the final branch merge.

import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/config/backend_config.dart';
import 'package:flutter_test_app/models/capture_session.dart';
import 'package:flutter_test_app/services/calibration_audio_service.dart';
import 'package:flutter_test_app/services/calibration_capture_coordinator.dart';
import 'package:flutter_test_app/services/sdacs_api_service.dart';
import 'package:flutter_test_app/state/operation_controller.dart';

class CoordinatorAudioFake implements CalibrationAudio {
  final positions = StreamController<Duration>.broadcast();
  final durations = StreamController<Duration?>.broadcast();
  final states = StreamController<CalibrationPlaybackState>.broadcast();
  bool ready = false;
  bool failPlay = false;
  CalibrationSignal? preloadedSignal;
  CalibrationSignal? playedSignal;
  int stopCalls = 0;

  @override
  bool get isReady => ready;
  @override
  Stream<Duration?> get durationStream => durations.stream;
  @override
  Stream<CalibrationPlaybackState> get playerStateStream => states.stream;
  @override
  Stream<Duration> get positionStream => positions.stream;

  @override
  Future<void> initialize() async => ready = true;

  @override
  Future<Duration?> preload(CalibrationSignal signal) async {
    preloadedSignal = signal;
    return const Duration(seconds: 60);
  }

  @override
  Future<void> play(CalibrationSignal signal) async {
    if (failPlay) throw StateError('browser audio blocked');
    playedSignal = signal;
    states.add(CalibrationPlaybackState.playing);
  }

  @override
  Future<void> playFrequencySweep() => play(CalibrationSignal.frequencySweep);
  @override
  Future<void> playOneKhzTone() => play(CalibrationSignal.oneKhzTone);
  @override
  Future<void> stop() async => stopCalls++;
  @override
  Future<void> dispose() async {}
}

class CoordinatorApiFake extends SdacsApiService {
  CoordinatorApiFake(this.scheduledStart)
    : super(config: const BackendConfig());

  final DateTime scheduledStart;
  int? requestedSeconds;
  int? requestedDelay;
  String? requestedValidationLabel;
  int statusCalls = 0;
  bool failTerminal = false;
  bool allowTerminal = false;

  @override
  Future<CaptureSession> startCapture({
    required int delayMs,
    required int recordSeconds,
    String? validationLabel,
    String? requestId,
  }) async {
    requestedDelay = delayMs;
    requestedSeconds = recordSeconds;
    requestedValidationLabel = validationLabel;
    return CaptureSession(
      captureId: 'capture_calibration_01',
      status: 'requested',
      requestedAt: scheduledStart.subtract(const Duration(seconds: 5)),
      scheduledStartAt: scheduledStart,
      durationSeconds: recordSeconds,
      completedNodes: const [],
      missingNodes: const [],
    );
  }

  @override
  Future<CaptureSession> getCapture(String captureId) async {
    statusCalls++;
    final terminal = allowTerminal && statusCalls >= 2;
    return CaptureSession(
      captureId: captureId,
      status: terminal
          ? failTerminal
                ? 'processing_failed'
                : 'complete'
          : 'processing',
      requestedAt: scheduledStart.subtract(const Duration(seconds: 5)),
      scheduledStartAt: scheduledStart,
      durationSeconds: 60,
      completedNodes: terminal
          ? const ['node01', 'node02', 'node03', 'node04']
          : const [],
      missingNodes: const [],
      failureReason: failTerminal ? 'processor unavailable' : null,
    );
  }

  @override
  Future<CaptureCombinedResult?> getCaptureResult(String captureId) async {
    return CaptureCombinedResult({
      'capture_id': captureId,
      'acoustic_analysis': {
        'status': 'complete',
        'node_metrics': {
          for (var index = 1; index <= 4; index++)
            'node0$index': {
              'node_id': 'node0$index',
              'mean_estimated_spl_db': 90.0 + index,
              'cal_offset_db': 120.0,
            },
        },
      },
    });
  }
}

void main() {
  testWidgets(
    '1 kHz calibration preloads audio, requests 60 seconds, and uses schedule',
    (tester) async {
      final now = tester.binding.clock.now().toUtc();
      final audio = CoordinatorAudioFake();
      final api = CoordinatorApiFake(now.add(const Duration(seconds: 5)));
      final operations = OperationController();
      final coordinator = CalibrationCaptureCoordinator(
        api: api,
        audio: audio,
        operations: operations,
        now: () => tester.binding.clock.now().toUtc(),
        pollInterval: const Duration(seconds: 1),
        nodesReady: () async => true,
      );

      final session = await coordinator.start(CalibrationSignal.oneKhzTone);
      expect(session?.captureId, 'capture_calibration_01');
      expect(audio.preloadedSignal, CalibrationSignal.oneKhzTone);
      expect(api.requestedSeconds, 60);
      expect(api.requestedDelay, 5000);
      expect(api.requestedValidationLabel, 'calibration_1khz');
      expect(operations.activeCaptureId, 'capture_calibration_01');

      await tester.pump(const Duration(seconds: 5));
      expect(audio.playedSignal, CalibrationSignal.oneKhzTone);
      expect(
        operations.operation,
        anyOf(
          ActiveOperation.calibrationCapturing,
          ActiveOperation.calibrationProcessing,
        ),
      );
      expect(operations.audioStartSkew?.inMilliseconds.abs(), lessThan(251));

      api.allowTerminal = true;
      await tester.pump(const Duration(seconds: 2));
      expect(operations.operation, ActiveOperation.idle);
      expect(operations.latestCalibrationSignal, CalibrationSignal.oneKhzTone);
      expect(operations.latestCalibrationResult?.nodeMetrics, hasLength(4));
      await coordinator.dispose();
    },
    timeout: const Timeout(Duration(seconds: 10)),
  );

  testWidgets(
    'sweep uses its asset but does not unlock SPL calibration',
    (tester) async {
      final now = tester.binding.clock.now().toUtc();
      final audio = CoordinatorAudioFake();
      final api = CoordinatorApiFake(now);
      final operations = OperationController();
      final coordinator = CalibrationCaptureCoordinator(
        api: api,
        audio: audio,
        operations: operations,
        now: () => tester.binding.clock.now().toUtc(),
        pollInterval: const Duration(seconds: 1),
        nodesReady: () async => true,
      );

      await coordinator.start(CalibrationSignal.frequencySweep);
      await tester.pump(const Duration(milliseconds: 1));
      await tester.pump();
      expect(audio.playedSignal, CalibrationSignal.frequencySweep);
      api.allowTerminal = true;
      await tester.pump(const Duration(seconds: 2));
      expect(
        operations.latestCalibrationSignal,
        CalibrationSignal.frequencySweep,
      );
      await coordinator.dispose();
    },
    timeout: const Timeout(Duration(seconds: 10)),
  );

  testWidgets(
    'audio-start failure preserves capture ID and invalidates run',
    (tester) async {
      final now = tester.binding.clock.now().toUtc();
      final audio = CoordinatorAudioFake()..failPlay = true;
      final api = CoordinatorApiFake(now);
      final operations = OperationController();
      final coordinator = CalibrationCaptureCoordinator(
        api: api,
        audio: audio,
        operations: operations,
        now: () => tester.binding.clock.now().toUtc(),
        pollInterval: const Duration(seconds: 1),
        nodesReady: () async => true,
      );

      await coordinator.start(CalibrationSignal.oneKhzTone);
      await tester.pump(const Duration(milliseconds: 1));
      await tester.pump();
      expect(operations.calibrationValid, isFalse);
      expect(operations.lastCalibrationCaptureId, 'capture_calibration_01');
      expect(operations.calibrationWarning, contains('Audio failed to start'));
      api.allowTerminal = true;
      await tester.pump(const Duration(seconds: 2));
      expect(operations.operation, ActiveOperation.idle);
      expect(operations.latestCalibrationResult, isNull);
      await coordinator.dispose();
    },
    timeout: const Timeout(Duration(seconds: 10)),
  );
}
