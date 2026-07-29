import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/app/app_routes.dart';
import 'package:flutter_test_app/screens/operator_tools/operator_tools_screen.dart';
import 'package:flutter_test_app/screens/main/main_screen.dart';
import 'package:flutter_test_app/services/calibration_audio_service.dart';
import 'package:flutter_test_app/state/operation_controller.dart';

class FakeCalibrationAudio implements CalibrationAudio {
  final positionController = StreamController<Duration>.broadcast();
  final durationController = StreamController<Duration?>.broadcast();
  final stateController =
      StreamController<CalibrationPlaybackState>.broadcast();

  String? requestedAsset;
  int stopCalls = 0;
  bool disposed = false;
  bool ready = false;

  @override
  bool get isReady => ready;

  @override
  Future<void> initialize() async {
    ready = true;
  }

  @override
  Future<Duration?> preload(CalibrationSignal signal) async {
    requestedAsset = signal == CalibrationSignal.oneKhzTone
        ? CalibrationAudio.oneKhzAsset
        : CalibrationAudio.frequencySweepAsset;
    return const Duration(seconds: 60);
  }

  @override
  Future<void> play(CalibrationSignal signal) =>
      signal == CalibrationSignal.oneKhzTone
      ? playOneKhzTone()
      : playFrequencySweep();

  @override
  Stream<Duration?> get durationStream => durationController.stream;

  @override
  Stream<CalibrationPlaybackState> get playerStateStream =>
      stateController.stream;

  @override
  Stream<Duration> get positionStream => positionController.stream;

  @override
  Future<void> playFrequencySweep() async {
    requestedAsset = CalibrationAudio.frequencySweepAsset;
    stateController.add(CalibrationPlaybackState.playing);
  }

  @override
  Future<void> playOneKhzTone() async {
    requestedAsset = CalibrationAudio.oneKhzAsset;
    stateController.add(CalibrationPlaybackState.playing);
  }

  @override
  Future<void> stop() async {
    stopCalls++;
    stateController.add(CalibrationPlaybackState.stopped);
  }

  @override
  Future<void> dispose() async {
    disposed = true;
    await positionController.close();
    await durationController.close();
    await stateController.close();
  }
}

void main() {
  late FakeCalibrationAudio audio;
  late OperationController operations;

  setUp(() {
    audio = FakeCalibrationAudio();
    operations = OperationController();
  });

  Widget app() => MaterialApp(
    routes: {
      '/': (_) => OperatorToolsScreen(
        audioService: audio,
        operationController: operations,
      ),
      AppRoutes.setup: (_) =>
          const Scaffold(body: Text('Existing Setup Screen')),
      AppRoutes.calibration: (_) =>
          const Scaffold(body: Text('Existing Calibration Screen')),
    },
  );

  test('calibration audio asset constants use the registered WAV paths', () {
    expect(
      CalibrationAudio.oneKhzAsset,
      'assets/audio/1khz_60s_continuous.wav',
    );
    expect(
      CalibrationAudio.frequencySweepAsset,
      'assets/audio/log_sine_sweep_up_down_60s.wav',
    );
  });

  testWidgets('main actions replace Setup and Calibrate with Operator Tools', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        routes: {
          AppRoutes.operatorTools: (_) =>
              const Scaffold(body: Text('Operator destination')),
        },
        home: Scaffold(
          body: MainHeroSection(
            onStartTest: () {},
            onScanBle: () {},
            isBleScanning: false,
            capture: null,
            layoutDirty: false,
            isCaptureActive: false,
            isOperationBlocking: false,
            canStartTest: true,
            canOpenOperatorTools: true,
            canStartBleScan: true,
            canOpenResults: false,
            resultsCaptureId: null,
          ),
        ),
      ),
    );

    expect(find.text('Operator Tools'), findsOneWidget);
    expect(find.text('Setup'), findsNothing);
    expect(find.text('Calibrate'), findsNothing);
    expect(find.text('Capture'), findsOneWidget);
    expect(find.text('Test'), findsNothing);
    expect(find.text('Bluetooth RSSI'), findsOneWidget);
    expect(find.text('Results'), findsOneWidget);

    await tester.tap(find.text('Operator Tools'));
    await tester.pumpAndSettle();
    expect(find.text('Operator destination'), findsOneWidget);
  });

  testWidgets('Operator Tools exposes setup and existing calibration', (
    tester,
  ) async {
    await tester.pumpWidget(app());

    expect(find.text('Operator Tools'), findsOneWidget);
    expect(find.text('Room and Node Setup'), findsOneWidget);
    expect(find.text('Advanced Calibration'), findsOneWidget);
    expect(find.text('Calibration Signals'), findsOneWidget);
    expect(find.text('Run 1 kHz Calibration Capture'), findsOneWidget);
    expect(find.text('Run Sweep Calibration Capture'), findsOneWidget);
    expect(find.text('SPL Calibration'), findsOneWidget);

    await tester.tap(find.byKey(const Key('open-room-setup')));
    await tester.pumpAndSettle();
    expect(find.text('Existing Setup Screen'), findsOneWidget);

    Navigator.of(tester.element(find.text('Existing Setup Screen'))).pop();
    await tester.pumpAndSettle();
    final calibrationButton = find.byKey(const Key('open-calibration'));
    await tester.ensureVisible(calibrationButton);
    await tester.tap(calibrationButton);
    await tester.pumpAndSettle();
    expect(find.text('Existing Calibration Screen'), findsOneWidget);
  });

  testWidgets('tone playback reports progress, stops, and unlocks controls', (
    tester,
  ) async {
    await tester.pumpWidget(app());
    final toneButton = find.byKey(const Key('play-oneKhzTone'));
    await tester.ensureVisible(toneButton);
    await tester.tap(toneButton);
    await tester.pump();

    expect(audio.requestedAsset, CalibrationAudio.oneKhzAsset);
    expect(operations.operation, ActiveOperation.audioPreview);
    expect(
      tester
          .widget<ButtonStyleButton>(find.byKey(const Key('open-room-setup')))
          .onPressed,
      isNull,
    );
    expect(
      tester
          .widget<ButtonStyleButton>(find.byKey(const Key('open-calibration')))
          .onPressed,
      isNull,
    );

    audio.durationController.add(const Duration(seconds: 60));
    audio.positionController.add(const Duration(seconds: 18));
    await tester.pump();
    expect(find.text('00:18 / 01:00'), findsOneWidget);
    expect(find.text('42 seconds remaining'), findsOneWidget);

    final stopButton = find.byKey(const Key('stop-active-signal'));
    await tester.ensureVisible(stopButton);
    await tester.tap(stopButton);
    await tester.pump();
    expect(audio.stopCalls, 1);
    expect(operations.operation, ActiveOperation.idle);
    expect(find.textContaining('Previewing:'), findsNothing);
  });

  testWidgets('preview locks the other signal and completion returns to idle', (
    tester,
  ) async {
    await tester.pumpWidget(app());
    final toneButton = find.byKey(const Key('play-oneKhzTone'));
    await tester.ensureVisible(toneButton);
    await tester.tap(toneButton);
    await tester.pump();
    final sweepButton = find.byKey(const Key('play-frequencySweep'));
    await tester.ensureVisible(sweepButton);
    expect(tester.widget<ButtonStyleButton>(sweepButton).onPressed, isNull);

    audio.stateController.add(CalibrationPlaybackState.completed);
    await tester.pump();
    expect(operations.operation, ActiveOperation.idle);
    expect(operations.activeSignal, CalibrationSignal.none);
    expect(find.textContaining('Previewing:'), findsNothing);
  });

  testWidgets('explicit back confirms and stops active playback', (
    tester,
  ) async {
    await tester.pumpWidget(app());
    final toneButton = find.byKey(const Key('play-oneKhzTone'));
    await tester.ensureVisible(toneButton);
    await tester.tap(toneButton);
    await tester.pump();
    await tester.tap(find.byTooltip('Back'));
    await tester.pumpAndSettle();

    expect(
      find.text('Stop the calibration signal and leave Operator Tools?'),
      findsOneWidget,
    );
    await tester.tap(find.text('Stop and Leave'));
    await tester.pumpAndSettle();
    expect(audio.stopCalls, 1);
    expect(operations.operation, ActiveOperation.idle);
  });

  testWidgets('signal cards stack without overflow on narrow screens', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(320, 700);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(app());
    expect(tester.takeException(), isNull);
    expect(find.text('1 kHz Reference Tone'), findsOneWidget);
    expect(find.text('20 Hz–20 kHz Logarithmic Sweep'), findsOneWidget);
  });
}
