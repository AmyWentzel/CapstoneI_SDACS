import 'dart:async';

import '../models/capture_session.dart';
import '../services/calibration_audio_service.dart';
import '../services/sdacs_api_service.dart';
import '../state/operation_controller.dart';

class CalibrationCaptureCoordinator {
  CalibrationCaptureCoordinator({
    required this.api,
    required this.audio,
    required this.operations,
    this.now = DateTime.now,
    this.pollInterval = const Duration(seconds: 3),
    this.nodesReady,
  });

  static const captureDuration = Duration(seconds: 60);
  static const startDelayMs = 5000;
  static const skewWarningThreshold = Duration(milliseconds: 250);

  final SdacsApiService api;
  final CalibrationAudio audio;
  final OperationController operations;
  final DateTime Function() now;
  final Duration pollInterval;
  final Future<bool> Function()? nodesReady;

  Timer? _audioStartTimer;
  Timer? _countdownTimer;
  Timer? _pollTimer;
  Timer? _timeoutTimer;
  StreamSubscription<Duration>? _positionSubscription;
  StreamSubscription<Duration?>? _durationSubscription;
  StreamSubscription<CalibrationPlaybackState>? _playerSubscription;
  DateTime? _effectiveStart;
  CalibrationSignal _signal = CalibrationSignal.none;

  Future<CaptureSession?> start(CalibrationSignal signal) async {
    if (!operations.begin(
      ActiveOperation.calibrationAudioPreparing,
      signal: signal,
    )) {
      return null;
    }
    _signal = signal;
    try {
      final readyCheck = nodesReady;
      final fourNodesReady = readyCheck != null
          ? await readyCheck()
          : (await api.getNodes())
                .map((node) => node.nodeId.toLowerCase())
                .toSet()
                .containsAll(const {'node01', 'node02', 'node03', 'node04'});
      if (!fourNodesReady) {
        throw const SdacsApiException(
          'All four SDACS nodes and the backend must be available.',
        );
      }
      await audio.initialize();
      final duration = await audio.preload(signal);
      if (!audio.isReady) {
        throw const SdacsApiException(
          'Calibration audio could not be enabled.',
        );
      }
      if (duration != null &&
          (duration - captureDuration).inMilliseconds.abs() > 500) {
        throw SdacsApiException(
          'Calibration audio must be approximately 60 seconds; '
          'reported ${duration.inMilliseconds / 1000} seconds.',
        );
      }
      operations.updateCalibration(
        audioReady: true,
        playbackDuration: duration ?? captureDuration,
        calibrationValid: true,
      );
      operations.transition(
        ActiveOperation.calibrationCaptureScheduling,
        signal: signal,
      );

      final session = await api.startCapture(
        delayMs: startDelayMs,
        recordSeconds: captureDuration.inSeconds,
        validationLabel: signal == CalibrationSignal.oneKhzTone
            ? 'calibration_1khz'
            : 'calibration_sweep',
        requestId:
            'capture_${now().toUtc().toIso8601String().replaceAll(RegExp(r'[-:.]'), '')}',
      );
      final scheduled = session.scheduledStartProvided
          ? session.scheduledStartAt.toUtc()
          : null;
      _effectiveStart = scheduled ?? now().toUtc();
      operations.updateCalibration(
        captureId: session.captureId,
        scheduledStart: scheduled,
        usedFallbackTiming: scheduled == null,
        warning: scheduled == null
            ? 'Scheduled start unavailable; local audio timing was used.'
            : null,
      );
      _listenToAudio();
      _scheduleAudio(session.captureId, scheduled);
      _startPolling(session.captureId);
      _startTimeout(session.captureId);
      return session;
    } catch (error) {
      await audio.stop();
      operations.invalidateCalibration(
        'Calibration capture could not start: $error',
      );
      operations.finish();
      rethrow;
    }
  }

  void _listenToAudio() {
    _positionSubscription ??= audio.positionStream.listen((position) {
      operations.updateCalibration(playbackPosition: position);
    });
    _durationSubscription ??= audio.durationStream.listen((duration) {
      if (duration != null) {
        operations.updateCalibration(playbackDuration: duration);
      }
    });
    _playerSubscription ??= audio.playerStateStream.listen((state) {
      if (state == CalibrationPlaybackState.completed) {
        unawaited(audio.stop());
      }
    });
  }

  void _scheduleAudio(String captureId, DateTime? scheduled) {
    final delay = scheduled?.difference(now().toUtc()) ?? Duration.zero;
    _audioStartTimer?.cancel();
    _audioStartTimer = Timer(delay.isNegative ? Duration.zero : delay, () async {
      if (operations.activeCaptureId != captureId) return;
      try {
        await audio.play(_signal);
        final started = now().toUtc();
        final target = scheduled ?? started;
        final skew = started.difference(target);
        operations.updateCalibration(
          audioStartedAt: started,
          audioStartSkew: skew,
          calibrationValid: true,
          warning: skew.abs() > skewWarningThreshold
              ? 'Calibration audio started ${skew.inMilliseconds.abs()} ms '
                    '${skew.isNegative ? 'before' : 'after'} the scheduled capture start.'
              : null,
        );
        operations.transition(
          ActiveOperation.calibrationCapturing,
          signal: _signal,
        );
        _startCountdown();
      } catch (error) {
        operations.invalidateCalibration(
          'Audio failed to start for capture $captureId: $error',
        );
        operations.transition(
          ActiveOperation.calibrationCapturing,
          signal: _signal,
        );
        _startCountdown();
      }
    });
  }

  void _startCountdown() {
    _countdownTimer?.cancel();
    void update() {
      final start = _effectiveStart;
      if (start == null) return;
      final elapsed = now().toUtc().difference(start);
      final remaining = captureDuration - elapsed;
      final position = remaining.isNegative
          ? captureDuration
          : elapsed.isNegative
          ? Duration.zero
          : elapsed;
      operations.updateCalibration(playbackPosition: position);
      if (remaining <= Duration.zero) {
        _countdownTimer?.cancel();
        unawaited(audio.stop());
        operations.transition(
          ActiveOperation.calibrationProcessing,
          signal: _signal,
        );
      }
    }

    update();
    _countdownTimer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => update(),
    );
  }

  void _startPolling(String captureId) {
    _pollTimer?.cancel();
    _pollTimer = Timer.periodic(pollInterval, (_) async {
      try {
        final session = await api.getCapture(captureId);
        if (operations.activeCaptureId != captureId) return;
        if (session.status == 'processing' ||
            session.status == 'capture_complete' ||
            session.processingStage?.contains('processing') == true) {
          operations.transition(
            ActiveOperation.calibrationProcessing,
            signal: _signal,
          );
        }
        if (!session.isTerminal) return;
        _pollTimer?.cancel();
        _timeoutTimer?.cancel();
        _countdownTimer?.cancel();
        _audioStartTimer?.cancel();
        await audio.stop();
        final failed = const {
          'failed',
          'collection_failed',
          'processing_failed',
        }.contains(session.status);
        if (failed) {
          operations.invalidateCalibration(
            session.failureReason ?? 'Calibration capture failed.',
          );
          operations.finish();
          return;
        }
        final result = await api.getCaptureResult(captureId);
        if (result == null || result.captureId != captureId) return;
        if (operations.calibrationValid) {
          operations.completeCalibration(result, _signal);
        }
        operations.finish();
      } on SdacsApiException {
        // Transient polling failures keep the backend-authoritative run active.
      }
    });
  }

  void _startTimeout(String captureId) {
    _timeoutTimer?.cancel();
    _timeoutTimer = Timer(const Duration(minutes: 3), () async {
      if (operations.activeCaptureId != captureId) return;
      _pollTimer?.cancel();
      _countdownTimer?.cancel();
      _audioStartTimer?.cancel();
      await audio.stop();
      operations.invalidateCalibration(
        'Calibration capture processing timed out for $captureId.',
      );
      operations.finish();
    });
  }

  Future<void> dispose() async {
    _audioStartTimer?.cancel();
    _countdownTimer?.cancel();
    _pollTimer?.cancel();
    _timeoutTimer?.cancel();
    unawaited(_positionSubscription?.cancel());
    unawaited(_durationSubscription?.cancel());
    unawaited(_playerSubscription?.cancel());
    await audio.stop();
  }
}
