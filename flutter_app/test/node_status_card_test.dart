// SDACS Flutter verification: regression coverage for node status card test.
// These tests protect operator-visible behavior during the final branch merge.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/capture_node_metric.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';
import 'package:flutter_test_app/screens/main/widgets/node_status_card.dart';

Widget _card(
  NodeTelemetry telemetry, {
  CaptureNodeMetric? captureMetric,
  double width = 360,
  double height = 280,
}) {
  return MaterialApp(
    home: Scaffold(
      body: SizedBox(
        width: width,
        height: height,
        child: NodeStatusCard(
          telemetry: telemetry,
          captureMetric: captureMetric,
        ),
      ),
    ),
  );
}

NodeTelemetry _populated({double spl = 32.61}) => NodeTelemetry(
  nodeId: 'node01',
  status: 'online',
  dbSpl: spl,
  calOffsetDb: 120.0,
  rms: 0.000045,
  dbfs: -87.39,
  peakFrequencyHz: 46.9,
  temperatureC: 24.87,
  humidityPercent: 46.77,
  batterySoc: 98.13,
  batteryValid: true,
  firmwareVersion: 'ota-enable-v2',
  wifiRssiDbm: -43,
  bleRssiDbm: -67,
  timestamp: DateTime.now(),
);

CaptureNodeMetric _metric({double spl = 32.61}) => CaptureNodeMetric(
  nodeId: 'node01',
  sampleCount: 59,
  estimatedSplDb: spl,
  rms: 0.000045,
  dbfs: -87.39,
  peakFrequencyHz: 46.9,
);

void main() {
  testWidgets('populated telemetry renders real values and precise RMS', (
    tester,
  ) async {
    await tester.pumpWidget(_card(_populated(), captureMetric: _metric()));
    expect(find.text('32.6 dB SPL'), findsOneWidget);
    expect(find.text('46.9 Hz'), findsOneWidget);
    await tester.tap(find.text('Details'));
    await tester.pump();
    expect(find.textContaining('RMS: 0.000045'), findsOneWidget);
    expect(find.textContaining('dBFS: -87.4 dBFS'), findsOneWidget);
    expect(find.textContaining('Calibration offset: 120.0 dB'), findsOneWidget);
    expect(find.textContaining('Temperature: 24.9 °C'), findsOneWidget);
    expect(find.textContaining('Humidity: 46.8% RH'), findsOneWidget);
    expect(find.textContaining('Battery: 98%'), findsOneWidget);
    expect(find.textContaining('BLE RSSI: -67 dBm'), findsOneWidget);
    expect(find.textContaining('Firmware: ota-enable-v2'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('missing telemetry uses semantic placeholders, never zeros', (
    tester,
  ) async {
    await tester.pumpWidget(
      _card(const NodeTelemetry(nodeId: 'node01', status: 'online')),
    );
    expect(find.text('—'), findsOneWidget);
    expect(find.text('No data'), findsOneWidget);
    await tester.tap(find.text('Details'));
    await tester.pump();
    expect(find.textContaining('Firmware: Unknown'), findsOneWidget);
    expect(find.textContaining('0.0 dB SPL'), findsNothing);
    expect(find.textContaining('0 Hz'), findsNothing);
    expect(find.textContaining('0.0% RH'), findsNothing);
  });

  testWidgets('constrained card has no RenderFlex overflow', (tester) async {
    await tester.pumpWidget(
      _card(_populated(), captureMetric: _metric(), width: 230, height: 180),
    );
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Details'));
    await tester.pump();
    expect(tester.takeException(), isNull);
  });

  test('partial environmental update preserves acoustic values', () {
    final acoustic = NodeTelemetry.fromJson({
      'node_id': 'node01',
      'record_type': 'features',
      'db_spl': 32.61,
      'cal_offset_db': 120.0,
      'rms': 0.000045,
      'dbfs': -87.39,
      'f_peak_hz': 46.9,
    });
    final environment = NodeTelemetry.fromJson({
      'node_id': 'SDACS-node01',
      'record_type': 'environment',
      'temp_c': 24.87,
      'rh_percent': 46.77,
    });
    final merged = acoustic.merge(environment);
    expect(merged.nodeId, 'node01');
    expect(merged.dbSpl, 32.61);
    expect(merged.calOffsetDb, 120.0);
    expect(merged.rms, 0.000045);
    expect(merged.peakFrequencyHz, 46.9);
    expect(merged.temperatureC, 24.87);
    expect(merged.humidityPercent, 46.77);
  });

  test('null heartbeat and fuel-gauge fields preserve live telemetry', () {
    final current = NodeTelemetry.fromJson({
      'node_id': 'node01',
      'status': 'online',
      'temp_c': 30.2,
      'rh_percent': 40.2,
      'batt_soc_percent': 96.7,
      'fw_version': 'ota-enable-v2',
      'rssi_dbm': -44,
      'ble_rssi_dbm': -67,
    });
    final heartbeat = NodeTelemetry.fromJson({
      'node_id': 'node01',
      'status': 'online',
      'temp_c': null,
      'rh_percent': null,
    });
    final fuelGauge = NodeTelemetry.fromJson({
      'node_id': 'node01',
      'batt_soc_percent': 0,
      'rh_percent': null,
    });
    final merged = current.merge(heartbeat).merge(fuelGauge);
    expect(merged.temperatureC, 30.2);
    expect(merged.humidityPercent, 40.2);
    expect(merged.batterySoc, 0);
    expect(merged.firmwareVersion, 'ota-enable-v2');
    expect(merged.wifiRssiDbm, -44);
    expect(merged.bleRssiDbm, -67);
  });

  testWidgets('state refresh rebuilds the card after a telemetry update', (
    tester,
  ) async {
    final metric = ValueNotifier<CaptureNodeMetric>(_metric());
    await tester.pumpWidget(
      MaterialApp(
        home: ValueListenableBuilder<CaptureNodeMetric>(
          valueListenable: metric,
          builder: (_, value, _) => SizedBox(
            width: 360,
            height: 220,
            child: NodeStatusCard(
              telemetry: _populated(),
              captureMetric: value,
            ),
          ),
        ),
      ),
    );
    expect(find.text('32.6 dB SPL'), findsOneWidget);
    metric.value = _metric(spl: 41.25);
    await tester.pump();
    expect(find.text('41.3 dB SPL'), findsOneWidget);
    expect(find.text('32.6 dB SPL'), findsNothing);
    metric.dispose();
  });

  testWidgets('healthy i2s timeout is shown as a non-fatal warning', (
    tester,
  ) async {
    await tester.pumpWidget(
      _card(
        const NodeTelemetry(
          nodeId: 'node03',
          sceneDbfs: -64.26,
          sceneRms: 0.000612,
          effectiveSampleRateHz: 47872,
          sampleRateOk: true,
          sceneMetricsValid: true,
          sceneClippedSampleCount: 0,
          hpfEnabled: true,
          hpfCutoffHz: 150,
          hpfOrder: 4,
          micSoftwareGain: 8,
          sceneSoftwareGain: 16,
          audioError: 'i2s_timeouts',
        ),
        height: 420,
      ),
    );
    await tester.tap(find.text('Details'));
    await tester.pump();

    expect(find.text('Scene Processing'), findsOneWidget);
    expect(find.textContaining('Sample rate status: OK'), findsOneWidget);
    expect(find.text('I2S polling warnings'), findsOneWidget);
    expect(find.textContaining('Audio error:'), findsNothing);
  });
}
