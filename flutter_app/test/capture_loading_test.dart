import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/screens/loading/loading_screen.dart';
import 'package:flutter_test_app/screens/main/main_screen.dart';
import 'package:flutter_test_app/state/operation_controller.dart';
import 'package:flutter_test_app/widgets/sdacs_logo.dart';

void main() {
  testWidgets('startup screen shows SDACS branding and progress', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(home: LoadingScreen(autoStart: false)),
    );

    expect(find.byType(SdacsLogo), findsOneWidget);
    expect(find.text('SDACS'), findsOneWidget);
    expect(
      find.text('Smart Distributed Acoustic Calibration System'),
      findsOneWidget,
    );
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    final logo = tester.widget<Image>(
      find.descendant(of: find.byType(SdacsLogo), matching: find.byType(Image)),
    );
    expect((logo.image as AssetImage).assetName, SdacsLogo.assetPath);
    expect(SdacsLogo.assetPath, 'assets/images/sdacs_logo_transparent.png');
    expect(logo.fit, BoxFit.contain);
  });

  testWidgets('successful startup waits seven seconds and navigates once', (
    tester,
  ) async {
    var initializationCalls = 0;
    await tester.pumpWidget(
      MaterialApp(
        routes: {
          '/': (_) => LoadingScreen(
            initialize: () async {
              initializationCalls++;
              return true;
            },
          ),
          '/main': (_) => const Scaffold(body: Text('Main ready')),
        },
      ),
    );

    await tester.pump();
    expect(initializationCalls, 1);
    await tester.pump(const Duration(seconds: 6, milliseconds: 999));
    expect(find.byType(LoadingScreen), findsOneWidget);
    await tester.pump(const Duration(milliseconds: 1));
    await tester.pumpAndSettle();
    expect(find.text('Main ready'), findsOneWidget);
    expect(initializationCalls, 1);
  });

  testWidgets('failed startup does not navigate after seven seconds', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        routes: {
          '/': (_) => LoadingScreen(initialize: () async => false),
          '/main': (_) => const Scaffold(body: Text('Main ready')),
        },
      ),
    );

    await tester.pump();
    await tester.pump(const Duration(seconds: 7));
    expect(find.byType(LoadingScreen), findsOneWidget);
    expect(find.text('Main ready'), findsNothing);
    expect(find.text('Unable to connect to the SDACS broker.'), findsOneWidget);
  });

  test('capture timing counts down from the synchronized start', () {
    final start = DateTime.utc(2026, 7, 28, 12);
    final timing = CaptureTiming(
      totalDuration: const Duration(seconds: 60),
      scheduledStart: start,
    );

    expect(
      timing.remainingAt(start.subtract(const Duration(seconds: 5))),
      const Duration(seconds: 60),
    );
    expect(
      timing.remainingAt(start.add(const Duration(seconds: 15))),
      const Duration(seconds: 45),
    );
    expect(
      timing.remainingAt(start.add(const Duration(seconds: 61))),
      Duration.zero,
    );
    expect(timing.fractionAt(start), 1);
    expect(timing.fractionAt(start.add(const Duration(seconds: 30))), 0.5);
    expect(timing.fractionAt(start.add(const Duration(seconds: 60))), 0);
  });

  test('active operations authoritatively lock all main actions', () {
    for (final operation in const [
      ActiveOperation.captureScheduling,
      ActiveOperation.capturing,
      ActiveOperation.captureProcessing,
      ActiveOperation.bleScanning,
      ActiveOperation.calibrating,
      ActiveOperation.audioPreview,
      ActiveOperation.calibrationAudioPreparing,
      ActiveOperation.calibrationCaptureScheduling,
      ActiveOperation.calibrationCapturing,
      ActiveOperation.calibrationProcessing,
    ]) {
      final access = OperationAccess(
        operation: operation,
        hasCompletedCapture: true,
        layoutDirty: false,
        calibrationConfigured: true,
      );

      expect(
        access.canOpenOperatorTools,
        isFalse,
        reason: '$operation Operator Tools',
      );
      expect(
        access.canStartCalibration,
        isFalse,
        reason: '$operation Calibrate',
      );
      expect(access.canStartTest, isFalse, reason: '$operation Test');
      expect(access.canStartBleScan, isFalse, reason: '$operation BLE');
      expect(access.canOpenResults, isFalse, reason: '$operation Results');
    }
  });

  test('idle access requires a valid result and configured calibration', () {
    const withoutResult = OperationAccess(
      operation: ActiveOperation.idle,
      hasCompletedCapture: false,
      layoutDirty: false,
    );
    const withResult = OperationAccess(
      operation: ActiveOperation.idle,
      hasCompletedCapture: true,
      layoutDirty: false,
    );

    expect(withoutResult.canOpenOperatorTools, isTrue);
    expect(withoutResult.canStartTest, isTrue);
    expect(withoutResult.canStartBleScan, isTrue);
    expect(withoutResult.canOpenResults, isFalse);
    expect(withoutResult.canStartCalibration, isFalse);
    expect(withResult.canOpenResults, isTrue);
  });

  testWidgets('capture overlay shows countdown and blocks dismissal', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              ModalBarrier(
                key: Key('capture-modal-barrier'),
                dismissible: false,
              ),
              CaptureProgressOverlay(
                phase: CapturePhase.capturing,
                captureId: 'capture-newest',
                totalDuration: Duration(seconds: 60),
                remainingDuration: Duration(seconds: 42),
              ),
            ],
          ),
        ),
      ),
    );

    expect(find.byType(SdacsLogo), findsOneWidget);
    expect(find.text('Capture in Progress'), findsOneWidget);
    expect(find.text('Synchronized Room Measurement'), findsOneWidget);
    expect(find.text('42 seconds remaining'), findsOneWidget);
    expect(find.text('capture-newest'), findsOneWidget);
    expect(
      find.text(
        'Keep the room quiet and avoid moving nodes until processing is complete.',
      ),
      findsNothing,
    );
    final logo = tester.widget<Image>(
      find.descendant(of: find.byType(SdacsLogo), matching: find.byType(Image)),
    );
    expect((logo.image as AssetImage).assetName, SdacsLogo.assetPath);
    expect(logo.fit, BoxFit.contain);
    expect(
      tester
          .widget<ModalBarrier>(find.byKey(const Key('capture-modal-barrier')))
          .dismissible,
      isFalse,
    );
  });

  testWidgets('processing overlay retains branding and processing state', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: CaptureProgressOverlay(
          phase: CapturePhase.processing,
          captureId: 'capture-newest',
          totalDuration: Duration(seconds: 60),
          remainingDuration: Duration.zero,
        ),
      ),
    );

    expect(find.byType(SdacsLogo), findsOneWidget);
    expect(find.text('Capture Complete'), findsOneWidget);
    expect(find.text('Processing Acoustic Results...'), findsOneWidget);
  });

  testWidgets('capture overlay remains bounded on a narrow screen', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(320, 568);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(
      const MaterialApp(
        home: CaptureProgressOverlay(
          phase: CapturePhase.capturing,
          captureId:
              'capture_20260728_very_long_but_still_valid_operator_identifier',
          totalDuration: Duration(seconds: 60),
          remainingDuration: Duration(seconds: 36),
        ),
      ),
    );

    expect(tester.takeException(), isNull);
    expect(find.text('36 seconds remaining'), findsOneWidget);
    expect(find.byType(SdacsLogo), findsOneWidget);
  });
}
