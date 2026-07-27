import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/capture_session.dart';
import 'package:flutter_test_app/screens/graphs/graphs_screen.dart';
import 'package:flutter_test_app/services/sdacs_api_service.dart';

class _FakeApi extends SdacsApiService {
  _FakeApi({required this.sessions, this.result});

  final List<CaptureSession> sessions;
  final CaptureCombinedResult? result;
  final List<String> captureRequests = [];
  final List<String> resultRequests = [];
  var _sessionIndex = 0;

  @override
  Future<CaptureSession> getCapture(String captureId) async {
    captureRequests.add(captureId);
    final index = _sessionIndex.clamp(0, sessions.length - 1);
    _sessionIndex++;
    return sessions[index];
  }

  @override
  Future<CaptureCombinedResult?> getCaptureResult(String captureId) async {
    resultRequests.add(captureId);
    return result;
  }

  @override
  Uri capturePlotUri(String captureId, {int? cacheBust}) =>
      Uri.parse('http://test/api/captures/$captureId/plot?v=$cacheBust');
}

CaptureSession _session({
  String id = 'capture_20260727T213846246Z',
  String status = 'acoustic_only',
  String stage = 'complete',
  List<String> completed = const ['node01', 'node02', 'node03', 'node04'],
  List<String> missing = const [],
  String? failureStage,
  String? failureReason,
}) => CaptureSession(
  captureId: id,
  status: status,
  requestedAt: DateTime.utc(2026, 7, 27),
  scheduledStartAt: DateTime.utc(2026, 7, 27),
  durationSeconds: 60,
  completedNodes: completed,
  missingNodes: missing,
  processingStage: stage,
  modelStatus: 'disabled',
  failureStage: failureStage,
  failureReason: failureReason,
);

CaptureCombinedResult _result({
  String id = 'capture_20260727T213846246Z',
  String acousticStatus = 'complete',
  int nodes = 4,
}) {
  final nodeIds = ['node01', 'node02', 'node03', 'node04'].take(nodes).toList();
  final missing = [
    'node01',
    'node02',
    'node03',
    'node04',
  ].where((id) => !nodeIds.contains(id)).toList();
  return CaptureCombinedResult({
    'capture_id': id,
    'status': nodes == 4 ? 'acoustic_only' : 'partial',
    'acoustic_analysis': {
      'status': acousticStatus,
      'generated_at': '2026-07-27T21:39:00Z',
      'nodes_used': nodeIds,
      'missing_nodes': missing,
      'mean_estimated_spl_db': 33.1,
      'dominant_band': 'low',
      'dominant_frequency_hz': 106.7,
      'loudest_node_id': nodeIds.last,
      'quietest_node_id': nodeIds.first,
      'spatial_variation_db': 0.59,
      'plot_filename': 'acoustic_map.png',
      'warnings': [
        if (nodes < 4) 'Reduced spatial reliability.',
        'Low-frequency estimates should be interpreted cautiously.',
      ],
      'node_metrics': {
        for (var index = 0; index < nodeIds.length; index++)
          nodeIds[index]: {
            'node_id': nodeIds[index],
            'sample_count': 59 - index,
            'mean_estimated_spl_db': 33.1 + index / 10,
            'peak_frequency_hz': 119 - index * 10,
          },
      },
    },
    'edge_impulse': {
      'status': 'disabled',
      'predicted_label': null,
      'confidence': null,
      'scores': <String, dynamic>{},
      'warnings': ['Edge Impulse inference is disabled.'],
    },
    'fusion': {
      'agreement': 'unavailable',
      'recommendation_confidence': 'unavailable',
    },
    'recommendation': {
      'summary':
          'Acoustic analysis is complete. The final Edge Impulse model is not currently configured.',
    },
  });
}

Widget _screen(
  _FakeApi api, {
  String id = 'capture_20260727T213846246Z',
  bool mockPlot = true,
}) => MaterialApp(
  home: GraphsScreen(
    arguments: CaptureResultsArguments(captureId: id),
    service: api,
    pollInterval: const Duration(milliseconds: 20),
    plotBuilder: mockPlot ? (uri) => Text('PLOT ${uri.path}') : null,
  ),
);

void _useLargeViewport(WidgetTester tester) {
  tester.view.physicalSize = const Size(1200, 2600);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
}

void main() {
  testWidgets('renders a successful four-node acoustic-only capture', (
    tester,
  ) async {
    _useLargeViewport(tester);
    final api = _FakeApi(sessions: [_session()], result: _result());
    await tester.pumpWidget(_screen(api));
    await tester.pumpAndSettle();

    expect(api.captureRequests, ['capture_20260727T213846246Z']);
    expect(api.resultRequests, ['capture_20260727T213846246Z']);
    expect(find.text('Collection: Complete'), findsWidgets);
    expect(find.text('Acoustic processing: Complete'), findsOneWidget);
    expect(find.text('Edge Impulse: Disabled'), findsOneWidget);
    expect(find.text('Dominant band: low'), findsOneWidget);
    expect(find.text('Loudest node: node04'), findsOneWidget);
    expect(find.text('Spatial variation: 0.6 dB'), findsOneWidget);
    for (var index = 0; index < 4; index++) {
      expect(
        find.text(
          'Estimated SPL: ${(33.1 + index / 10).toStringAsFixed(1)} dB',
        ),
        findsOneWidget,
      );
      expect(
        find.text(
          'Peak frequency: ${(119 - index * 10).toStringAsFixed(1)} Hz',
        ),
        findsOneWidget,
      );
    }
    expect(find.textContaining('Low-frequency estimates'), findsOneWidget);
    expect(find.textContaining('PLOT /api/captures/'), findsOneWidget);
    expect(find.textContaining('0%'), findsNothing);
  });

  testWidgets('polls a processing capture without false completion', (
    tester,
  ) async {
    _useLargeViewport(tester);
    final api = _FakeApi(
      sessions: [
        _session(status: 'processing', stage: 'processing_acoustic_analysis'),
        _session(),
      ],
      result: _result(),
    );
    await tester.pumpWidget(_screen(api));
    await tester.pump();
    expect(find.text('Acoustic processing is running…'), findsOneWidget);
    expect(find.text('Acoustic analysis is complete.'), findsNothing);
    expect(find.text('Acoustic processing failed'), findsNothing);
    await tester.pump(const Duration(milliseconds: 25));
    await tester.pumpAndSettle();
    expect(api.captureRequests.length, 2);
  });

  testWidgets('shows backend processing failure separately from collection', (
    tester,
  ) async {
    _useLargeViewport(tester);
    final api = _FakeApi(
      sessions: [
        _session(
          status: 'failed',
          stage: 'processing_acoustic_analysis',
          failureStage: 'capture_data_extraction',
          failureReason: 'No usable acoustic feature rows were found.',
        ),
      ],
    );
    await tester.pumpWidget(_screen(api));
    await tester.pumpAndSettle();
    expect(find.text('Collection: Complete'), findsOneWidget);
    expect(find.text('Acoustic processing failed'), findsOneWidget);
    expect(find.text('Acoustic processing: Failed'), findsOneWidget);
    expect(find.text('Failure stage: capture_data_extraction'), findsOneWidget);
    expect(
      find.text('Reason: No usable acoustic feature rows were found.'),
      findsOneWidget,
    );
    expect(find.text('Capture plot'), findsNothing);
    expect(find.textContaining('Acoustic analysis is complete'), findsNothing);
    expect(find.text('Refresh'), findsOneWidget);
  });

  testWidgets('partial capture shows only returned nodes and its plot', (
    tester,
  ) async {
    _useLargeViewport(tester);
    final api = _FakeApi(
      sessions: [
        _session(
          status: 'partial',
          completed: const ['node01', 'node02', 'node03'],
          missing: const ['node04'],
        ),
      ],
      result: _result(acousticStatus: 'partial', nodes: 3),
    );
    await tester.pumpWidget(_screen(api));
    await tester.pumpAndSettle();
    expect(find.text('Collection: Partial'), findsWidgets);
    expect(find.text('Acoustic processing: Partial'), findsOneWidget);
    expect(find.text('Partial data: missing node04'), findsOneWidget);
    expect(find.text('node04'), findsNothing);
    expect(find.textContaining('0.0 dB'), findsNothing);
    expect(find.textContaining('PLOT /api/captures/'), findsOneWidget);
  });

  testWidgets('invalid capture ID performs no backend request', (tester) async {
    _useLargeViewport(tester);
    final api = _FakeApi(sessions: [_session()]);
    await tester.pumpWidget(_screen(api, id: 'unknown'));
    await tester.pump();
    expect(api.captureRequests, isEmpty);
    expect(api.resultRequests, isEmpty);
    expect(find.text('The capture ID is invalid.'), findsOneWidget);
  });

  testWidgets('rejects a stale result for another capture', (tester) async {
    _useLargeViewport(tester);
    final api = _FakeApi(
      sessions: [_session()],
      result: _result(id: 'capture_other'),
    );
    await tester.pumpWidget(_screen(api));
    await tester.pumpAndSettle();
    expect(find.textContaining('different capture'), findsOneWidget);
    expect(find.text('Dominant band: low'), findsNothing);
  });

  testWidgets('capture plot exposes loading failure and retry states', (
    tester,
  ) async {
    _useLargeViewport(tester);
    final api = _FakeApi(sessions: [_session()], result: _result());
    await tester.pumpWidget(_screen(api, mockPlot: false));
    await tester.pump();
    await tester.pump();
    expect(
      find.text('Loading capture plot…').evaluate().isNotEmpty ||
          find.text('Capture plot could not be loaded.').evaluate().isNotEmpty,
      isTrue,
    );
    await tester.pumpAndSettle();
    expect(find.text('Capture plot could not be loaded.'), findsOneWidget);
    expect(find.text('Retry'), findsOneWidget);
  });

  testWidgets('disposing a processing screen cancels polling', (tester) async {
    _useLargeViewport(tester);
    final api = _FakeApi(
      sessions: [_session(status: 'processing', stage: 'processing')],
    );
    await tester.pumpWidget(_screen(api));
    await tester.pump();
    final requestsBeforeDispose = api.captureRequests.length;
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pump(const Duration(milliseconds: 100));
    expect(api.captureRequests.length, requestsBeforeDispose);
  });
}
