import 'package:flutter/foundation.dart';

import '../models/capture_session.dart';

enum ActiveOperation {
  idle,
  captureScheduling,
  capturing,
  captureProcessing,
  bleScanning,
  calibrating,
  audioPreview,
  calibrationAudioPreparing,
  calibrationCaptureScheduling,
  calibrationCapturing,
  calibrationProcessing,
}

enum CalibrationSignal { none, oneKhzTone, frequencySweep }

class OperationAccess {
  const OperationAccess({
    required this.operation,
    required this.hasCompletedCapture,
    required this.layoutDirty,
    this.calibrationConfigured = false,
  });

  final ActiveOperation operation;
  final bool hasCompletedCapture;
  final bool layoutDirty;
  final bool calibrationConfigured;

  bool get isCaptureActive => const {
    ActiveOperation.captureScheduling,
    ActiveOperation.capturing,
    ActiveOperation.captureProcessing,
  }.contains(operation);
  bool get isBleScanning => operation == ActiveOperation.bleScanning;
  bool get isCalibrationActive => operation == ActiveOperation.calibrating;
  bool get isSignalPlaying => operation == ActiveOperation.audioPreview;
  bool get isCalibrationCapture => const {
    ActiveOperation.calibrationAudioPreparing,
    ActiveOperation.calibrationCaptureScheduling,
    ActiveOperation.calibrationCapturing,
    ActiveOperation.calibrationProcessing,
  }.contains(operation);
  bool get isOperationBlocking => operation != ActiveOperation.idle;
  bool get canOpenOperatorTools => !isOperationBlocking;
  bool get canStartTest => !isOperationBlocking && !layoutDirty;
  bool get canStartBleScan => !isOperationBlocking;
  bool get canStartCalibration => calibrationConfigured && !isOperationBlocking;
  bool get canOpenResults => !isOperationBlocking && hasCompletedCapture;
}

class OperationController extends ChangeNotifier {
  OperationController();

  static final OperationController instance = OperationController();

  ActiveOperation _operation = ActiveOperation.idle;
  CalibrationSignal _activeSignal = CalibrationSignal.none;
  String? _activeCaptureId;
  String? _lastCalibrationCaptureId;
  DateTime? _scheduledCaptureStart;
  DateTime? _audioStartedAt;
  Duration? _audioStartSkew;
  bool _audioReady = false;
  Duration _playbackPosition = Duration.zero;
  Duration _playbackDuration = const Duration(seconds: 60);
  bool _calibrationValid = false;
  bool _usedFallbackTiming = false;
  CaptureCombinedResult? _latestCalibrationResult;
  CalibrationSignal _latestCalibrationSignal = CalibrationSignal.none;
  String? _calibrationWarning;
  int _generation = 0;

  ActiveOperation get operation => _operation;
  CalibrationSignal get activeSignal => _activeSignal;
  String? get activeCaptureId => _activeCaptureId;
  String? get lastCalibrationCaptureId => _lastCalibrationCaptureId;
  DateTime? get scheduledCaptureStart => _scheduledCaptureStart;
  DateTime? get audioStartedAt => _audioStartedAt;
  Duration? get audioStartSkew => _audioStartSkew;
  bool get audioReady => _audioReady;
  Duration get playbackPosition => _playbackPosition;
  Duration get playbackDuration => _playbackDuration;
  bool get calibrationValid => _calibrationValid;
  bool get usedFallbackTiming => _usedFallbackTiming;
  CaptureCombinedResult? get latestCalibrationResult =>
      _latestCalibrationResult;
  CalibrationSignal get latestCalibrationSignal => _latestCalibrationSignal;
  String? get calibrationWarning => _calibrationWarning;
  int get generation => _generation;

  bool begin(
    ActiveOperation operation, {
    CalibrationSignal signal = CalibrationSignal.none,
  }) {
    if (_operation != ActiveOperation.idle) return false;
    _generation++;
    _set(operation, signal);
    return true;
  }

  void transition(
    ActiveOperation operation, {
    CalibrationSignal signal = CalibrationSignal.none,
  }) {
    _set(operation, signal);
  }

  void updateCalibration({
    String? captureId,
    DateTime? scheduledStart,
    DateTime? audioStartedAt,
    Duration? audioStartSkew,
    bool? audioReady,
    Duration? playbackPosition,
    Duration? playbackDuration,
    bool? calibrationValid,
    bool? usedFallbackTiming,
    String? warning,
  }) {
    _activeCaptureId = captureId ?? _activeCaptureId;
    if (captureId != null) _lastCalibrationCaptureId = captureId;
    _scheduledCaptureStart = scheduledStart ?? _scheduledCaptureStart;
    _audioStartedAt = audioStartedAt ?? _audioStartedAt;
    _audioStartSkew = audioStartSkew ?? _audioStartSkew;
    _audioReady = audioReady ?? _audioReady;
    _playbackPosition = playbackPosition ?? _playbackPosition;
    _playbackDuration = playbackDuration ?? _playbackDuration;
    _calibrationValid = calibrationValid ?? _calibrationValid;
    _usedFallbackTiming = usedFallbackTiming ?? _usedFallbackTiming;
    _calibrationWarning = warning ?? _calibrationWarning;
    notifyListeners();
  }

  void completeCalibration(
    CaptureCombinedResult result,
    CalibrationSignal signal,
  ) {
    _latestCalibrationResult = result;
    _latestCalibrationSignal = signal;
    _calibrationValid = true;
    notifyListeners();
  }

  void invalidateCalibration(String warning) {
    _calibrationValid = false;
    _calibrationWarning = warning;
    notifyListeners();
  }

  void finish() {
    _generation++;
    _activeCaptureId = null;
    _scheduledCaptureStart = null;
    _audioStartedAt = null;
    _audioStartSkew = null;
    _audioReady = false;
    _playbackPosition = Duration.zero;
    _playbackDuration = const Duration(seconds: 60);
    _usedFallbackTiming = false;
    _set(ActiveOperation.idle, CalibrationSignal.none);
  }

  void _set(ActiveOperation operation, CalibrationSignal signal) {
    if (_operation == operation && _activeSignal == signal) return;
    _operation = operation;
    _activeSignal = signal;
    notifyListeners();
  }
}
