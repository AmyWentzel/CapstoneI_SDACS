import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../models/ble_scan_result.dart';
import '../../models/calibration_result.dart';
import '../../models/capture_session.dart';
import '../../models/capture_node_metric.dart';
import '../../models/node_telemetry.dart';
import '../../services/sdacs_api_service.dart';
import '../../services/websocket_telemetry_service.dart';
import '../../state/operation_controller.dart';
import '../../widgets/sdacs_error_banner.dart';
import '../../widgets/sdacs_logo.dart';
import 'widgets/latest_calibration_card.dart';
import 'widgets/node_status_card.dart';
import 'widgets/editable_room_layout.dart';
import '../graphs/graphs_screen.dart';

enum CapturePhase { idle, scheduling, capturing, processing }

class CaptureTiming {
  const CaptureTiming({
    required this.totalDuration,
    required this.scheduledStart,
  });

  final Duration totalDuration;
  final DateTime scheduledStart;

  Duration remainingAt(DateTime now) {
    final elapsed = now.toUtc().difference(scheduledStart.toUtc());
    if (elapsed.isNegative) return totalDuration;
    final remaining = totalDuration - elapsed;
    return remaining.isNegative ? Duration.zero : remaining;
  }

  double fractionAt(DateTime now) {
    if (totalDuration.inMilliseconds <= 0) return 0;
    return (remainingAt(now).inMilliseconds / totalDuration.inMilliseconds)
        .clamp(0.0, 1.0);
  }
}

class MainScreen extends StatefulWidget {
  const MainScreen({super.key, this.operationController});

  final OperationController? operationController;

  static const background = Color(0xFF08080C);
  static const panel = Color(0xFF12121A);
  static const panelLight = Color(0xFF1A1A25);
  static const accent = Color(0xFF8B5CF6);
  static const accentLight = Color(0xFFA78BFA);
  static const accentDark = Color(0xFF6D28D9);
  static const textMuted = Color(0xFFA1A1AA);

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  static const int _testCaptureDurationSeconds = 60;
  static const int _synchronizedStartDelayMs = 5000;
  static const Duration _captureProcessingTimeout = Duration(minutes: 2);

  final Map<String, NodeTelemetry> _nodesById = {};
  BackendConfig? _activeConfig;
  SdacsApiService? _apiService;
  WebSocketTelemetryService? _telemetryService;
  StreamSubscription<NodeTelemetry>? _telemetrySubscription;
  bool _isLoading = true;
  bool _backendOnline = false;
  bool _isSubmittingTest = false;
  late final OperationController _operationController;
  int _latestBleScanRequestId = 0;
  DateTime? _latestBleScanTimestamp;
  Timer? _capturePollTimer;
  Timer? _captureCountdownTimer;
  Timer? _captureTimeoutTimer;
  CapturePhase _capturePhase = CapturePhase.idle;
  CaptureTiming? _captureTiming;
  Duration _remainingCaptureDuration = Duration.zero;
  int _captureGeneration = 0;
  CaptureSession? _activeCapture;
  Map<String, CaptureNodeMetric> _latestCaptureMetricsByNode = const {};
  String? _latestCaptureId;
  DateTime? _latestCaptureGeneratedAt;
  String? _errorMessage;
  bool _layoutDirty = false;

  OperationAccess get _operationAccess => OperationAccess(
    operation: _operationController.operation,
    hasCompletedCapture: _latestCaptureId != null,
    layoutDirty: _layoutDirty || _isSubmittingTest,
  );
  bool get _isCaptureActive => _operationAccess.isCaptureActive;
  bool get _isBleScanning => _operationAccess.isBleScanning;
  bool get _isOperationBlocking => _operationAccess.isOperationBlocking;
  bool get _canStartTest => _operationAccess.canStartTest;
  bool get _canStartBleScan => _operationAccess.canStartBleScan;
  bool get _canOpenResults => _operationAccess.canOpenResults;
  bool get _canOpenOperatorTools => _operationAccess.canOpenOperatorTools;

  List<NodeTelemetry> get _nodes {
    final nodes = _nodesById.values.toList()
      ..sort((a, b) => a.nodeId.compareTo(b.nodeId));
    return nodes;
  }

  @override
  void initState() {
    super.initState();
    _operationController =
        widget.operationController ?? OperationController.instance;
    _operationController.addListener(_handleOperationChanged);
  }

  void _handleOperationChanged() {
    if (!mounted) return;
    final calibrationResult = _operationController.latestCalibrationResult;
    setState(() {
      if (calibrationResult != null &&
          calibrationResult.captureId != _latestCaptureId) {
        _latestCaptureMetricsByNode = Map.unmodifiable(
          calibrationResult.nodeMetrics,
        );
        _latestCaptureId = calibrationResult.captureId;
        _latestCaptureGeneratedAt = calibrationResult.generatedAt;
      }
    });
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();

    final config = BackendConfigScope.configOf(context);
    if (_activeConfig == config) {
      return;
    }

    _activeConfig = config;
    _apiService = SdacsApiService(config: config);

    unawaited(_telemetrySubscription?.cancel());
    unawaited(_telemetryService?.disconnect());
    _telemetryService = WebSocketTelemetryService(config: config);
    _telemetrySubscription = _telemetryService!.telemetryStream.listen(
      _handleTelemetryUpdate,
      onError: (_) {},
    );
    unawaited(_telemetryService!.connect());
    unawaited(_loadInitialNodes());
  }

  @override
  void dispose() {
    _capturePollTimer?.cancel();
    _captureCountdownTimer?.cancel();
    _captureTimeoutTimer?.cancel();
    _operationController.removeListener(_handleOperationChanged);
    if (_operationAccess.isCaptureActive || _operationAccess.isBleScanning) {
      _operationController.finish();
    }
    unawaited(_telemetrySubscription?.cancel());
    unawaited(_telemetryService?.disconnect());
    super.dispose();
  }

  Future<void> _loadInitialNodes() async {
    final apiService = _apiService;
    if (apiService == null) {
      return;
    }

    setState(() {
      _isLoading = true;
    });

    try {
      final nodes = await apiService.getNodes();
      if (!mounted || apiService != _apiService) {
        return;
      }
      setState(() {
        final refreshed = <String, NodeTelemetry>{..._nodesById};
        for (final node in nodes) {
          refreshed[node.nodeId] = refreshed[node.nodeId]?.merge(node) ?? node;
        }
        _nodesById
          ..clear()
          ..addAll(refreshed);
        _backendOnline = true;
        _isLoading = false;
        _errorMessage = null;
      });
    } on SdacsApiException catch (error) {
      if (!mounted || apiService != _apiService) {
        return;
      }
      setState(() {
        _backendOnline = false;
        _isLoading = false;
        _errorMessage = error.message;
      });
    }
  }

  void _handleTelemetryUpdate(NodeTelemetry telemetry) {
    if (!mounted) {
      return;
    }
    setState(() {
      _nodesById[telemetry.nodeId] =
          _nodesById[telemetry.nodeId]?.merge(telemetry) ?? telemetry;
      _backendOnline = true;
      _errorMessage = null;
    });
  }

  Future<void> _startTest(BuildContext context) async {
    final apiService = _apiService;
    if (apiService == null || _isSubmittingTest || _isOperationBlocking) {
      return;
    }

    final generation = ++_captureGeneration;
    setState(() {
      _isSubmittingTest = true;
      _operationController.transition(ActiveOperation.captureScheduling);
      _capturePhase = CapturePhase.scheduling;
      _captureTiming = null;
      _remainingCaptureDuration = const Duration(
        seconds: _testCaptureDurationSeconds,
      );
    });

    final messenger = ScaffoldMessenger.of(context);
    _capturePollTimer?.cancel();
    setState(() => _activeCapture = null);
    final requestId =
        'capture_${DateTime.now().toUtc().toIso8601String().replaceAll(RegExp(r'[-:.]'), '')}';

    try {
      final session = await apiService.startCapture(
        delayMs: _synchronizedStartDelayMs,
        recordSeconds: _testCaptureDurationSeconds,
        requestId: requestId,
      );
      if (!mounted || generation != _captureGeneration) return;
      final seconds = session.durationSeconds > 0
          ? session.durationSeconds
          : _testCaptureDurationSeconds;
      final timing = CaptureTiming(
        totalDuration: Duration(seconds: seconds),
        scheduledStart: session.scheduledStartAt.toUtc(),
      );
      setState(() {
        _activeCapture = session;
        _captureTiming = timing;
        _remainingCaptureDuration = timing.remainingAt(DateTime.now().toUtc());
        _capturePhase = DateTime.now().toUtc().isBefore(timing.scheduledStart)
            ? CapturePhase.scheduling
            : CapturePhase.capturing;
      });
      _startCaptureCountdown(session.captureId, generation);
      _startCapturePolling(session.captureId, generation);
      _startCaptureTimeout(session.captureId, generation, timing);
      messenger.showSnackBar(
        SnackBar(
          content: Text(
            '$_testCaptureDurationSeconds-second capture request sent: '
            '${session.sessionId}',
          ),
        ),
      );
    } on SdacsApiException catch (error) {
      if (mounted && generation == _captureGeneration) {
        setState(() {
          _capturePhase = CapturePhase.idle;
          _operationController.finish();
          _captureTiming = null;
          _remainingCaptureDuration = Duration.zero;
        });
      }
      messenger.showSnackBar(
        SnackBar(content: Text('Capture failed: ${error.message}')),
      );
    } catch (error) {
      if (mounted && generation == _captureGeneration) {
        setState(() {
          _capturePhase = CapturePhase.idle;
          _operationController.finish();
          _captureTiming = null;
          _remainingCaptureDuration = Duration.zero;
        });
        messenger.showSnackBar(
          SnackBar(content: Text('Capture failed: $error')),
        );
      }
    } finally {
      if (mounted) {
        setState(() {
          _isSubmittingTest = false;
        });
      }
    }
  }

  void _startCaptureCountdown(String captureId, int generation) {
    _captureCountdownTimer?.cancel();
    void update() {
      if (!mounted ||
          generation != _captureGeneration ||
          _activeCapture?.captureId != captureId) {
        return;
      }
      final timing = _captureTiming;
      if (timing == null) return;
      final now = DateTime.now().toUtc();
      final remaining = timing.remainingAt(now);
      setState(() {
        _remainingCaptureDuration = remaining;
        if (now.isBefore(timing.scheduledStart)) {
          _capturePhase = CapturePhase.scheduling;
          _operationController.transition(ActiveOperation.captureScheduling);
        } else if (remaining > Duration.zero) {
          _capturePhase = CapturePhase.capturing;
          _operationController.transition(ActiveOperation.capturing);
        } else {
          _capturePhase = CapturePhase.processing;
          _operationController.transition(ActiveOperation.captureProcessing);
        }
      });
      if (remaining == Duration.zero) _captureCountdownTimer?.cancel();
    }

    update();
    if (_capturePhase == CapturePhase.processing) return;
    _captureCountdownTimer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => update(),
    );
  }

  void _startCapturePolling(String captureId, int generation) {
    _capturePollTimer?.cancel();
    _capturePollTimer = Timer.periodic(const Duration(seconds: 3), (_) async {
      final apiService = _apiService;
      if (apiService == null) return;
      try {
        final session = await apiService.getCapture(captureId);
        if (!mounted ||
            generation != _captureGeneration ||
            _activeCapture?.captureId != captureId) {
          return;
        }
        final isProcessing =
            session.status == 'processing' ||
            session.status == 'capture_complete' ||
            session.processingStage?.contains('processing') == true;
        if (isProcessing) {
          _captureCountdownTimer?.cancel();
        }
        setState(() {
          _activeCapture = session;
          if (isProcessing) {
            _capturePhase = CapturePhase.processing;
            _operationController.transition(ActiveOperation.captureProcessing);
          }
        });
        if (session.isTerminal) {
          _capturePollTimer?.cancel();
          _captureCountdownTimer?.cancel();
          _captureTimeoutTimer?.cancel();
          final failed = const {
            'failed',
            'processing_failed',
            'collection_failed',
          }.contains(session.status);
          setState(() {
            _capturePhase = CapturePhase.idle;
            _operationController.finish();
            _captureTiming = null;
            _remainingCaptureDuration = Duration.zero;
          });
          if (!failed) {
            unawaited(_loadCaptureResult(captureId));
          } else {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(
                  session.failureReason ??
                      'The latest Capture could not be processed; previous readings were retained.',
                ),
              ),
            );
          }
        }
      } on SdacsApiException {
        // Keep the current capture visible and retry transient failures.
      }
    });
  }

  void _startCaptureTimeout(
    String captureId,
    int generation,
    CaptureTiming timing,
  ) {
    _captureTimeoutTimer?.cancel();
    final untilStart = timing.scheduledStart.difference(DateTime.now().toUtc());
    final timeout =
        (untilStart.isNegative ? Duration.zero : untilStart) +
        timing.totalDuration +
        _captureProcessingTimeout;
    _captureTimeoutTimer = Timer(timeout, () {
      if (!mounted ||
          generation != _captureGeneration ||
          _activeCapture?.captureId != captureId ||
          !_isCaptureActive) {
        return;
      }
      _capturePollTimer?.cancel();
      _captureCountdownTimer?.cancel();
      setState(() {
        _capturePhase = CapturePhase.idle;
        _operationController.finish();
        _captureTiming = null;
        _remainingCaptureDuration = Duration.zero;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Capture processing timed out; previous readings were retained.',
          ),
        ),
      );
    });
  }

  Future<void> _loadCaptureResult(String captureId) async {
    final apiService = _apiService;
    if (apiService == null || _activeCapture?.captureId != captureId) return;
    try {
      final result = await apiService.getCaptureResult(captureId);
      if (!mounted ||
          result == null ||
          _activeCapture?.captureId != captureId ||
          result.captureId != captureId) {
        return;
      }
      setState(() {
        _latestCaptureMetricsByNode = Map.unmodifiable(result.nodeMetrics);
        _latestCaptureId = captureId;
        _latestCaptureGeneratedAt = result.generatedAt;
      });
    } on SdacsApiException catch (error) {
      if (!mounted || _activeCapture?.captureId != captureId) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Capture result unavailable: ${error.message}')),
      );
    }
  }

  Future<void> _scanBleNodes(BuildContext context) async {
    final apiService = _apiService;
    if (apiService == null || !_canStartBleScan) return;
    final requestId = ++_latestBleScanRequestId;
    _operationController.transition(ActiveOperation.bleScanning);
    final messenger = ScaffoldMessenger.of(context);
    try {
      final result = await apiService.scanBleNodes();
      if (!mounted ||
          apiService != _apiService ||
          !isCurrentBleScanRequest(requestId, _latestBleScanRequestId)) {
        return;
      }
      setState(() {
        _latestBleScanTimestamp = result.completedAt;
        final merged = mergeBleScanIntoLiveNodes(_nodesById, result);
        _nodesById
          ..clear()
          ..addAll(merged);
      });
      final message = result.detectedCount == 0
          ? 'BLE scan complete: no SDACS nodes detected.'
          : 'BLE scan complete: ${result.detectedCount} nodes detected at '
                '${_latestBleScanTimestamp!.toLocal().toIso8601String()}.';
      messenger.showSnackBar(SnackBar(content: Text(message)));
    } on SdacsApiException catch (error) {
      if (mounted &&
          isCurrentBleScanRequest(requestId, _latestBleScanRequestId)) {
        messenger.showSnackBar(
          SnackBar(content: Text('BLE scan failed: ${error.message}')),
        );
      }
    } catch (error) {
      if (mounted &&
          isCurrentBleScanRequest(requestId, _latestBleScanRequestId)) {
        messenger.showSnackBar(
          SnackBar(content: Text('BLE scan failed: $error')),
        );
      }
    } finally {
      if (mounted &&
          isCurrentBleScanRequest(requestId, _latestBleScanRequestId)) {
        if (_operationController.operation == ActiveOperation.bleScanning) {
          _operationController.finish();
        }
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final nodes = _nodes;
    final resultCaptureId = _latestCaptureId;

    return Theme(
      data: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: MainScreen.background,
        cardColor: MainScreen.panel,
        colorScheme: const ColorScheme.dark(
          primary: MainScreen.accent,
          secondary: MainScreen.accentLight,
          surface: MainScreen.panel,
        ),
      ),
      child: Scaffold(
        appBar: AppBar(
          title: const Text(
            'SDACS CONTROL CENTER',
            style: TextStyle(
              fontWeight: FontWeight.w900,
              letterSpacing: 1.6,
              fontSize: 15,
            ),
          ),
          centerTitle: true,
          backgroundColor: MainScreen.background,
          foregroundColor: Colors.white,
          elevation: 0,
          actions: [
            IconButton(
              tooltip: 'Refresh telemetry',
              icon: const Icon(Icons.refresh),
              onPressed: _isLoading
                  ? null
                  : () => unawaited(_loadInitialNodes()),
            ),
          ],
        ),
        body: Stack(
          children: [
            LayoutBuilder(
              builder: (context, constraints) {
                final isWide = constraints.maxWidth >= 1050;

                return SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(24, 10, 24, 32),
                  child: Center(
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 1320),
                      child: Column(
                        children: [
                          MainHeroSection(
                            onStartTest: () => _startTest(context),
                            onScanBle: () => _scanBleNodes(context),
                            isBleScanning: _isBleScanning,
                            capture: _activeCapture,
                            layoutDirty: _layoutDirty,
                            isCaptureActive: _isCaptureActive,
                            isOperationBlocking: _isOperationBlocking,
                            canStartTest: _canStartTest,
                            canOpenOperatorTools: _canOpenOperatorTools,
                            canStartBleScan: _canStartBleScan,
                            canOpenResults: _canOpenResults,
                            resultsCaptureId: resultCaptureId,
                          ),
                          if (_errorMessage != null) ...[
                            const SizedBox(height: 16),
                            SdacsErrorBanner(message: _errorMessage!),
                          ],
                          if (_isLoading) ...[
                            const SizedBox(height: 16),
                            const LinearProgressIndicator(),
                          ],
                          const SizedBox(height: 28),
                          isWide
                              ? Row(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                    Expanded(
                                      flex: 7,
                                      child: EditableRoomLayout(
                                        nodes: nodes,
                                        api: _apiService!,
                                        captureMetrics:
                                            _latestCaptureMetricsByNode,
                                        latestCaptureId: _latestCaptureId,
                                        onDirtyChanged: (dirty) {
                                          if (mounted &&
                                              dirty != _layoutDirty) {
                                            setState(
                                              () => _layoutDirty = dirty,
                                            );
                                          }
                                        },
                                      ),
                                    ),
                                    const SizedBox(width: 22),
                                    Expanded(
                                      flex: 3,
                                      child: _SystemPanel(
                                        nodes: nodes,
                                        backendOnline: _backendOnline,
                                        captureMetrics:
                                            _latestCaptureMetricsByNode,
                                      ),
                                    ),
                                  ],
                                )
                              : Column(
                                  children: [
                                    EditableRoomLayout(
                                      nodes: nodes,
                                      api: _apiService!,
                                      captureMetrics:
                                          _latestCaptureMetricsByNode,
                                      latestCaptureId: _latestCaptureId,
                                      onDirtyChanged: (dirty) {
                                        if (mounted && dirty != _layoutDirty) {
                                          setState(() => _layoutDirty = dirty);
                                        }
                                      },
                                    ),
                                    const SizedBox(height: 22),
                                    _SystemPanel(
                                      nodes: nodes,
                                      backendOnline: _backendOnline,
                                      captureMetrics:
                                          _latestCaptureMetricsByNode,
                                    ),
                                  ],
                                ),
                          const SizedBox(height: 28),
                          _LowerSection(
                            nodes: nodes,
                            captureMetrics: _latestCaptureMetricsByNode,
                            latestCaptureId: _latestCaptureId,
                            latestCaptureGeneratedAt: _latestCaptureGeneratedAt,
                          ),
                        ],
                      ),
                    ),
                  ),
                );
              },
            ),
            if (_isCaptureActive) ...[
              const Positioned.fill(
                child: ModalBarrier(
                  key: Key('capture-modal-barrier'),
                  dismissible: false,
                  color: Color(0xCC08080C),
                ),
              ),
              Positioned.fill(
                child: CaptureProgressOverlay(
                  phase: _capturePhase,
                  captureId: _activeCapture?.captureId,
                  totalDuration:
                      _captureTiming?.totalDuration ??
                      const Duration(seconds: _testCaptureDurationSeconds),
                  remainingDuration: _remainingCaptureDuration,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class CaptureProgressOverlay extends StatelessWidget {
  const CaptureProgressOverlay({
    super.key,
    required this.phase,
    required this.captureId,
    required this.totalDuration,
    required this.remainingDuration,
    this.calibrationSignal,
  });

  final CapturePhase phase;
  final String? captureId;
  final Duration totalDuration;
  final Duration remainingDuration;
  final String? calibrationSignal;

  @override
  Widget build(BuildContext context) {
    final totalMilliseconds = totalDuration.inMilliseconds;
    final progress = totalMilliseconds <= 0
        ? 0.0
        : (remainingDuration.inMilliseconds / totalMilliseconds).clamp(
            0.0,
            1.0,
          );
    final remainingSeconds = (remainingDuration.inMilliseconds / 1000)
        .ceil()
        .clamp(0, totalDuration.inSeconds);
    final isCalibration = calibrationSignal != null;
    final title = switch (phase) {
      CapturePhase.scheduling =>
        isCalibration
            ? 'Calibration Capture in Progress'
            : 'Preparing Synchronized Capture...',
      CapturePhase.capturing =>
        isCalibration
            ? 'Calibration Capture in Progress'
            : 'Capture in Progress',
      CapturePhase.processing =>
        isCalibration ? 'Calibration Capture Complete' : 'Capture Complete',
      CapturePhase.idle =>
        isCalibration ? 'Calibration Capture Complete' : 'Capture Complete',
    };
    final status = switch (phase) {
      CapturePhase.scheduling => 'The room measurement will begin shortly.',
      CapturePhase.capturing =>
        '$remainingSeconds ${remainingSeconds == 1 ? 'second' : 'seconds'} remaining',
      CapturePhase.processing => 'Processing Acoustic Results...',
      CapturePhase.idle => 'Returning to the dashboard...',
    };

    return Semantics(
      scopesRoute: true,
      namesRoute: true,
      explicitChildNodes: true,
      label: 'Synchronized capture progress',
      child: Material(
        color: Colors.transparent,
        child: SafeArea(
          child: Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(24),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 560),
                child: Container(
                  width: double.infinity,
                  padding: const EdgeInsets.symmetric(
                    horizontal: 32,
                    vertical: 30,
                  ),
                  decoration: BoxDecoration(
                    color: MainScreen.panel,
                    borderRadius: BorderRadius.circular(30),
                    border: Border.all(
                      color: MainScreen.accentLight.withValues(alpha: 0.35),
                    ),
                    boxShadow: const [
                      BoxShadow(
                        color: Colors.black54,
                        blurRadius: 32,
                        offset: Offset(0, 16),
                      ),
                    ],
                  ),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const SdacsLogo(size: 154),
                      const SizedBox(height: 24),
                      Text(
                        title,
                        textAlign: TextAlign.center,
                        style: Theme.of(context).textTheme.headlineSmall
                            ?.copyWith(
                              color: Colors.white,
                              fontWeight: FontWeight.w900,
                            ),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        calibrationSignal ?? 'Synchronized Room Measurement',
                        textAlign: TextAlign.center,
                        style: TextStyle(
                          color: MainScreen.accentLight,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      const SizedBox(height: 22),
                      Semantics(
                        label: 'Capture time remaining',
                        value: status,
                        child: LinearProgressIndicator(
                          value: phase == CapturePhase.processing
                              ? null
                              : progress,
                          minHeight: 10,
                          borderRadius: BorderRadius.circular(10),
                          backgroundColor: MainScreen.panelLight,
                        ),
                      ),
                      const SizedBox(height: 14),
                      Text(
                        status,
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 17,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      if (captureId != null) ...[
                        const SizedBox(height: 12),
                        SelectableText(
                          captureId!,
                          textAlign: TextAlign.center,
                          style: const TextStyle(
                            color: MainScreen.textMuted,
                            fontSize: 12,
                          ),
                        ),
                      ],
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class MainHeroSection extends StatelessWidget {
  const MainHeroSection({
    super.key,
    required this.onStartTest,
    required this.onScanBle,
    required this.isBleScanning,
    required this.capture,
    required this.layoutDirty,
    required this.isCaptureActive,
    required this.isOperationBlocking,
    required this.canStartTest,
    required this.canOpenOperatorTools,
    required this.canStartBleScan,
    required this.canOpenResults,
    required this.resultsCaptureId,
  });

  final VoidCallback onStartTest;
  final VoidCallback onScanBle;
  final bool isBleScanning;
  final CaptureSession? capture;
  final bool layoutDirty;
  final bool isCaptureActive;
  final bool isOperationBlocking;
  final bool canStartTest;
  final bool canOpenOperatorTools;
  final bool canStartBleScan;
  final bool canOpenResults;
  final String? resultsCaptureId;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 42),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(34),
        color: MainScreen.panel,
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF181020), Color(0xFF101018), Color(0xFF08080C)],
        ),
      ),
      child: Column(
        children: [
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: MainScreen.accent.withValues(alpha: 0.14),
              shape: BoxShape.circle,
            ),
            child: const Icon(
              Icons.graphic_eq,
              color: MainScreen.accentLight,
              size: 42,
            ),
          ),
          const SizedBox(height: 22),
          Text(
            'Smart Distributed Acoustic\nCalibration System',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.displaySmall?.copyWith(
              color: Colors.white,
              fontWeight: FontWeight.w900,
              height: 1.05,
            ),
          ),
          const SizedBox(height: 14),
          const Text(
            'Monitor room nodes, run calibration, capture SPL data, and analyze acoustic response from one clean dashboard.',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: MainScreen.textMuted,
              fontSize: 15,
              height: 1.5,
            ),
          ),
          if (capture != null) ...[
            const SizedBox(height: 20),
            Text(
              '${capture!.captureId} — ${_captureStatusLabel(capture!)}',
              textAlign: TextAlign.center,
              style: const TextStyle(color: MainScreen.accentLight),
            ),
          ],
          const SizedBox(height: 28),
          Wrap(
            alignment: WrapAlignment.center,
            spacing: 14,
            runSpacing: 14,
            children: [
              _ActionButton(
                icon: Icons.engineering_outlined,
                label: 'Operator Tools',
                filled: true,
                disabledReason: isOperationBlocking
                    ? 'Operator Tools unavailable during an active operation'
                    : null,
                onPressed: !canOpenOperatorTools
                    ? null
                    : () =>
                          Navigator.pushNamed(context, AppRoutes.operatorTools),
              ),
              _ActionButton(
                icon: Icons.play_arrow_rounded,
                label: 'Capture',
                disabledReason: isCaptureActive
                    ? 'Capture already in progress'
                    : isBleScanning
                    ? 'Capture unavailable during BLE scanning'
                    : layoutDirty
                    ? 'Save the room layout before capturing'
                    : null,
                onPressed: canStartTest ? onStartTest : null,
              ),
              _ActionButton(
                icon: Icons.bluetooth_searching,
                label: isBleScanning ? 'Scanning...' : 'Bluetooth RSSI',
                disabledReason: isCaptureActive
                    ? 'BLE unavailable during capture'
                    : isBleScanning
                    ? 'BLE scan already in progress'
                    : null,
                onPressed: canStartBleScan ? onScanBle : null,
                showProgress: isBleScanning,
              ),
              _ActionButton(
                icon: Icons.insights,
                label: 'Results',
                disabledReason: isCaptureActive
                    ? 'Results available after capture processing completes'
                    : isBleScanning
                    ? 'Results unavailable during BLE scanning'
                    : resultsCaptureId == null
                    ? 'Results require a completed capture'
                    : null,
                onPressed: !canOpenResults
                    ? null
                    : () => Navigator.pushNamed(
                        context,
                        AppRoutes.captureResults,
                        arguments: CaptureResultsArguments(
                          captureId: resultsCaptureId!,
                        ),
                      ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  String _captureStatusLabel(CaptureSession session) {
    const labels = {
      'requested': 'Capture requested',
      'capturing': 'Capturing',
      'waiting_for_nodes': 'Waiting for nodes',
      'processing': 'Processing',
      'complete': 'Results ready',
      'acoustic_only': 'Acoustic analysis ready; model unavailable',
      'partial': 'Partial',
      'failed': 'Failed',
    };
    return labels[session.processingStage] ??
        labels[session.status] ??
        session.status;
  }
}

class _ActionButton extends StatelessWidget {
  const _ActionButton({
    required this.icon,
    required this.label,
    required this.onPressed,
    this.filled = false,
    this.showProgress = false,
    this.disabledReason,
  });

  final IconData icon;
  final String label;
  final VoidCallback? onPressed;
  final bool filled;
  final bool showProgress;
  final String? disabledReason;

  @override
  Widget build(BuildContext context) {
    final button = SizedBox(
      height: 48,
      child: filled
          ? FilledButton.icon(
              style: FilledButton.styleFrom(
                backgroundColor: MainScreen.accent,
                foregroundColor: Colors.white,
                padding: const EdgeInsets.symmetric(horizontal: 22),
              ),
              icon: showProgress
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : Icon(icon),
              label: Text(label),
              onPressed: onPressed,
            )
          : OutlinedButton.icon(
              style: OutlinedButton.styleFrom(
                foregroundColor: Colors.white,
                side: BorderSide(
                  color: MainScreen.accentLight.withValues(alpha: 0.45),
                ),
                padding: const EdgeInsets.symmetric(horizontal: 22),
              ),
              icon: showProgress
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : Icon(icon),
              label: Text(label),
              onPressed: onPressed,
            ),
    );
    return Semantics(
      enabled: onPressed != null,
      button: true,
      label: label,
      hint: onPressed == null ? disabledReason : null,
      child: Tooltip(
        message: onPressed == null
            ? disabledReason ?? '$label unavailable'
            : label,
        child: button,
      ),
    );
  }
}

class LegacyRoomMap extends StatelessWidget {
  const LegacyRoomMap({
    super.key,
    required this.nodes,
    required this.bleResultsByNodeId,
    required this.hasBleScan,
  });

  final List<NodeTelemetry> nodes;
  final Map<String, BleNodeScanResult> bleResultsByNodeId;
  final bool hasBleScan;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 560,
      width: double.infinity,
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: MainScreen.panel,
        borderRadius: BorderRadius.circular(32),
      ),
      child: Stack(
        alignment: Alignment.center,
        children: [
          Positioned(
            top: 0,
            child: Column(
              children: [
                Text(
                  'Room Node Layout',
                  style: Theme.of(context).textTheme.titleLarge?.copyWith(
                    color: Colors.white,
                    fontWeight: FontWeight.w900,
                  ),
                ),
                const SizedBox(height: 6),
                const Text(
                  'Spatial view of live measurements around the room.',
                  style: TextStyle(color: MainScreen.textMuted),
                ),
              ],
            ),
          ),
          Positioned(
            top: 84,
            left: 42,
            right: 42,
            bottom: 54,
            child: Container(
              decoration: BoxDecoration(
                color: MainScreen.panelLight,
                borderRadius: BorderRadius.circular(34),
                border: Border.all(
                  color: MainScreen.accent.withValues(alpha: 0.35),
                ),
              ),
            ),
          ),
          const Positioned(top: 265, child: _SpeakerSource()),
          if (nodes.isNotEmpty)
            Positioned(top: 135, left: 85, child: _mapNode(nodes[0])),
          if (nodes.length > 1)
            Positioned(top: 135, right: 85, child: _mapNode(nodes[1])),
          if (nodes.length > 2)
            Positioned(bottom: 88, left: 85, child: _mapNode(nodes[2])),
          if (nodes.length > 3)
            Positioned(bottom: 88, right: 85, child: _mapNode(nodes[3])),
          if (nodes.isEmpty)
            const Center(
              child: Text(
                'No live nodes reported yet.',
                style: TextStyle(color: MainScreen.textMuted),
              ),
            ),
          const Positioned(bottom: 14, child: _SuggestionCard()),
        ],
      ),
    );
  }

  Widget _mapNode(NodeTelemetry node) => _MapNode(
    node: node,
    bleResult: bleResultsByNodeId[node.nodeId],
    hasBleScan: hasBleScan,
  );
}

class _SpeakerSource extends StatelessWidget {
  const _SpeakerSource();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 158,
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: MainScreen.background,
        borderRadius: BorderRadius.circular(26),
        border: Border.all(color: MainScreen.accent.withValues(alpha: 0.5)),
      ),
      child: const Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            Icons.speaker_group_outlined,
            color: MainScreen.accentLight,
            size: 40,
          ),
          SizedBox(height: 10),
          Text(
            'Test Source',
            style: TextStyle(color: Colors.white, fontWeight: FontWeight.w900),
          ),
          SizedBox(height: 3),
          Text(
            'Studio Monitor',
            style: TextStyle(color: MainScreen.textMuted, fontSize: 12),
          ),
        ],
      ),
    );
  }
}

class _MapNode extends StatelessWidget {
  const _MapNode({
    required this.node,
    required this.bleResult,
    required this.hasBleScan,
  });

  final NodeTelemetry node;
  final BleNodeScanResult? bleResult;
  final bool hasBleScan;

  String get bleRssiText => bleResult == null
      ? (hasBleScan ? 'BLE RSSI: Not detected' : 'BLE RSSI: Not scanned')
      : 'BLE RSSI: ${bleResult!.bleRssiDbm} dBm';

  Color get splColor {
    final spl = node.dbSpl;
    if (spl == null) return const Color(0xFFA1A1AA);
    if (spl >= 75) return const Color(0xFFFF6B6B);
    if (spl >= 68) return const Color(0xFFFFB86B);
    if (spl <= 55) return const Color(0xFFA1A1AA);
    return MainScreen.accentLight;
  }

  String get splStatus {
    final spl = node.dbSpl;
    if (spl == null) return 'No SPL data';
    if (spl >= 75) return 'Very High';
    if (spl >= 68) return 'High';
    if (spl <= 55) return 'Low';
    return 'Normal';
  }

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message:
          '${node.nodeId}: ${node.dbSpl?.toStringAsFixed(1) ?? 'no SPL data'}, '
          '${node.peakFrequencyHz?.toStringAsFixed(1) ?? 'no peak data'}; $bleRssiText',
      child: Container(
        width: 152,
        padding: const EdgeInsets.all(15),
        decoration: BoxDecoration(
          color: MainScreen.background,
          borderRadius: BorderRadius.circular(24),
          border: Border.all(color: splColor.withValues(alpha: 0.65)),
        ),
        child: Column(
          children: [
            Icon(Icons.sensors, color: splColor),
            const SizedBox(height: 8),
            Text(
              node.nodeId,
              style: const TextStyle(
                color: Colors.white,
                fontWeight: FontWeight.w900,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              node.dbSpl == null
                  ? 'Est. SPL: —'
                  : '${node.dbSpl!.toStringAsFixed(1)} dB est. SPL',
              style: TextStyle(color: splColor, fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: 2),
            Text(
              node.peakFrequencyHz == null
                  ? 'Peak: No data'
                  : '${node.peakFrequencyHz!.toStringAsFixed(1)} Hz peak',
              style: const TextStyle(color: MainScreen.textMuted, fontSize: 12),
            ),
            const SizedBox(height: 3),
            Text(
              bleRssiText,
              textAlign: TextAlign.center,
              style: const TextStyle(color: MainScreen.textMuted, fontSize: 11),
            ),
            const SizedBox(height: 10),
            LinearProgressIndicator(
              value: node.batterySoc == null
                  ? 0
                  : (node.batterySoc! / 100).clamp(0, 1).toDouble(),
              color: MainScreen.accent,
              backgroundColor: MainScreen.panelLight,
              minHeight: 5,
              borderRadius: BorderRadius.circular(20),
            ),
            const SizedBox(height: 6),
            Text(
              '$splStatus - ${node.batterySoc?.toStringAsFixed(0) ?? '—'}% battery',
              textAlign: TextAlign.center,
              style: const TextStyle(color: MainScreen.textMuted, fontSize: 11),
            ),
          ],
        ),
      ),
    );
  }
}

class _SuggestionCard extends StatelessWidget {
  const _SuggestionCard();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 460,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: MainScreen.accent.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: MainScreen.accent.withValues(alpha: 0.25)),
      ),
      child: const Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.lightbulb_outline, color: MainScreen.accentLight),
          SizedBox(width: 10),
          Flexible(
            child: Text(
              'Suggestion: compare front and rear node levels to identify uneven room response.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.white, fontSize: 12, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }
}

class _SystemPanel extends StatelessWidget {
  const _SystemPanel({
    required this.nodes,
    required this.backendOnline,
    required this.captureMetrics,
  });

  final List<NodeTelemetry> nodes;
  final bool backendOnline;
  final Map<String, CaptureNodeMetric> captureMetrics;

  @override
  Widget build(BuildContext context) {
    final batteries = nodes
        .map((node) => node.batterySoc)
        .whereType<double>()
        .toList();
    final avgBattery = batteries.isEmpty
        ? null
        : batteries.reduce((a, b) => a + b) / batteries.length;

    final captureSpl = captureMetrics.values
        .map((metric) => metric.estimatedSplDb)
        .whereType<double>()
        .where((value) => value.isFinite)
        .toList();
    final avgSpl = captureSpl.isEmpty
        ? null
        : captureSpl.reduce((a, b) => a + b) / captureSpl.length;

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: MainScreen.panel,
        borderRadius: BorderRadius.circular(32),
      ),
      child: Column(
        children: [
          const Text(
            'System Overview',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.w900,
              fontSize: 21,
            ),
          ),
          const SizedBox(height: 22),
          _MetricTile(
            label: 'Connected Nodes',
            value: '${nodes.length}',
            icon: Icons.hub_outlined,
          ),
          _MetricTile(
            label: captureSpl.isEmpty
                ? 'Average est. SPL'
                : 'Average est. SPL (${captureSpl.length} nodes)',
            value: avgSpl == null ? '—' : '${avgSpl.toStringAsFixed(1)} dB',
            icon: Icons.graphic_eq,
          ),
          _MetricTile(
            label: 'Average Battery',
            value: avgBattery == null ? '—' : '${avgBattery.round()}%',
            icon: Icons.battery_5_bar,
          ),
          _MetricTile(
            label: 'System State',
            value: backendOnline ? 'Live' : 'Offline',
            icon: Icons.check_circle_outline,
          ),
        ],
      ),
    );
  }
}

class _MetricTile extends StatelessWidget {
  const _MetricTile({
    required this.label,
    required this.value,
    required this.icon,
  });

  final String label;
  final String value;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: MainScreen.panelLight,
        borderRadius: BorderRadius.circular(20),
      ),
      child: Row(
        children: [
          Icon(icon, color: MainScreen.accentLight),
          const SizedBox(width: 14),
          Expanded(
            child: Text(
              label,
              style: const TextStyle(color: MainScreen.textMuted),
            ),
          ),
          Text(
            value,
            style: const TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.w900,
            ),
          ),
        ],
      ),
    );
  }
}

class _LowerSection extends StatelessWidget {
  const _LowerSection({
    required this.nodes,
    required this.captureMetrics,
    required this.latestCaptureId,
    required this.latestCaptureGeneratedAt,
  });

  final List<NodeTelemetry> nodes;
  final Map<String, CaptureNodeMetric> captureMetrics;
  final String? latestCaptureId;
  final DateTime? latestCaptureGeneratedAt;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        LatestCalibrationCard(result: CalibrationResult.placeholder()),
        const SizedBox(height: 22),
        GridView.builder(
          itemCount: nodes.length,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: MediaQuery.sizeOf(context).width >= 1100 ? 4 : 2,
            crossAxisSpacing: 14,
            mainAxisSpacing: 14,
            childAspectRatio: 1.45,
          ),
          itemBuilder: (context, index) {
            final node = nodes[index];
            return NodeStatusCard(
              telemetry: node,
              captureMetric: captureMetrics[node.nodeId],
              latestCaptureId: latestCaptureId,
              capturedAt: latestCaptureGeneratedAt,
            );
          },
        ),
      ],
    );
  }
}
