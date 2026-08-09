// SDACS Flutter verification: regression coverage for room layout card test.
// These tests protect operator-visible behavior during the final branch merge.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/capture_node_metric.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';
import 'package:flutter_test_app/screens/main/widgets/editable_room_layout.dart';
import 'package:flutter_test_app/services/sdacs_api_service.dart';

void main() {
  testWidgets(
    'compact cards show BLE state and source Broker without overflow',
    (tester) async {
      tester.view.physicalSize = const Size(760, 900);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final nodes = [
        const NodeTelemetry(nodeId: 'node01', bleRssiDbm: -67),
        for (var index = 2; index <= 4; index++)
          NodeTelemetry(nodeId: 'node0$index'),
      ];

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SizedBox(
              width: 760,
              height: 850,
              child: EditableRoomLayout(
                nodes: nodes,
                api: const SdacsApiService(),
                onDirtyChanged: (_) {},
                captureMetrics: const {
                  'node01': CaptureNodeMetric(
                    nodeId: 'node01',
                    sampleCount: 60,
                    estimatedSplDb: 33.1,
                    peakFrequencyHz: 119,
                  ),
                },
                latestCaptureId: 'capture_latest',
              ),
            ),
          ),
        ),
      );
      await tester.pump();

      expect(find.text('BLE RSSI: -67 dBm'), findsOneWidget);
      expect(find.text('BLE RSSI: No reading'), findsNWidgets(3));
      expect(find.text('Configured source'), findsOneWidget);
      expect(find.text('Broker'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );
}
