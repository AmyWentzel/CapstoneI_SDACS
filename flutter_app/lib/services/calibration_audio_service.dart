// SDACS Flutter component: Controls local calibration reference-tone playback used during the operator calibration workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'dart:async';

import 'package:audioplayers/audioplayers.dart';

import '../state/operation_controller.dart';

enum CalibrationPlaybackState { stopped, playing, paused, completed }

abstract interface class CalibrationAudio {
  static const oneKhzAsset = 'assets/audio/1khz_60s_continuous.wav';
  static const frequencySweepAsset =
      'assets/audio/log_sine_sweep_up_down_60s.wav';

  Stream<Duration> get positionStream;
  Stream<Duration?> get durationStream;
  Stream<CalibrationPlaybackState> get playerStateStream;
  bool get isReady;

  Future<void> initialize();
  Future<Duration?> preload(CalibrationSignal signal);
  Future<void> play(CalibrationSignal signal);
  Future<void> playOneKhzTone();
  Future<void> playFrequencySweep();
  Future<void> stop();
  Future<void> dispose();
}

class CalibrationAudioService implements CalibrationAudio {
  CalibrationAudioService({AudioPlayer? player})
    : _player = player ?? AudioPlayer();

  final AudioPlayer _player;
  final StreamController<CalibrationPlaybackState> _completionController =
      StreamController<CalibrationPlaybackState>.broadcast();
  StreamSubscription<void>? _completionSubscription;
  bool _isReady = false;

  @override
  bool get isReady => _isReady;

  @override
  Stream<Duration> get positionStream => _player.onPositionChanged;

  @override
  Stream<Duration?> get durationStream => _player.onDurationChanged;

  @override
  Stream<CalibrationPlaybackState> get playerStateStream =>
      Stream<CalibrationPlaybackState>.multi((controller) {
        final stateSubscription = _player.onPlayerStateChanged.listen(
          (state) => controller.add(_mapState(state)),
          onError: controller.addError,
        );
        final completionSubscription = _completionController.stream.listen(
          controller.add,
          onError: controller.addError,
        );
        controller.onCancel = () async {
          await stateSubscription.cancel();
          await completionSubscription.cancel();
        };
      }, isBroadcast: true);

  Future<void> _play(String assetPath) async {
    await stop();
    _completionSubscription ??= _player.onPlayerComplete.listen((_) {
      _completionController.add(CalibrationPlaybackState.completed);
    });
    await _player.play(
      AssetSource(assetPath.replaceFirst('assets/', '')),
      mode: PlayerMode.mediaPlayer,
    );
  }

  @override
  Future<void> initialize() async {
    await _player.setReleaseMode(ReleaseMode.stop);
    _isReady = true;
  }

  @override
  Future<Duration?> preload(CalibrationSignal signal) async {
    await initialize();
    final path = signal == CalibrationSignal.oneKhzTone
        ? CalibrationAudio.oneKhzAsset
        : CalibrationAudio.frequencySweepAsset;
    await _player.setSource(AssetSource(path.replaceFirst('assets/', '')));
    return _player.getDuration();
  }

  @override
  Future<void> play(CalibrationSignal signal) =>
      signal == CalibrationSignal.oneKhzTone
      ? playOneKhzTone()
      : playFrequencySweep();

  @override
  Future<void> playOneKhzTone() => _play(CalibrationAudio.oneKhzAsset);

  @override
  Future<void> playFrequencySweep() =>
      _play(CalibrationAudio.frequencySweepAsset);

  @override
  Future<void> stop() => _player.stop();

  @override
  Future<void> dispose() async {
    await _completionSubscription?.cancel();
    await _completionController.close();
    await _player.dispose();
  }

  CalibrationPlaybackState _mapState(PlayerState state) => switch (state) {
    PlayerState.playing => CalibrationPlaybackState.playing,
    PlayerState.paused => CalibrationPlaybackState.paused,
    PlayerState.stopped ||
    PlayerState.completed => CalibrationPlaybackState.stopped,
    PlayerState.disposed => CalibrationPlaybackState.stopped,
  };
}
