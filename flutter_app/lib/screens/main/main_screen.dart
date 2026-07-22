import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../models/ble_scan_result.dart';
import '../../models/calibration_result.dart';
import '../../models/node_telemetry.dart';
import '../../services/sdacs_api_service.dart';
import '../../services/websocket_telemetry_service.dart';
import '../../shared/sdacs_capture_labels.dart';
import '../../widgets/sdacs_error_banner.dart';
import 'widgets/latest_calibration_card.dart';
import 'widgets/node_status_card.dart';

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

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

  final Map<String, NodeTelemetry> _nodesById = {};
  final Map<String, BleNodeScanResult> _bleResultsByNodeId = {};

  String _selectedCaptureLabelId = 'speech';

  BackendConfig? _activeConfig;
  SdacsApiService? _apiService;
  WebSocketTelemetryService? _telemetryService;
  StreamSubscription<NodeTelemetry>? _telemetrySubscription;
  bool _isLoading = true;
  bool _backendOnline = false;
  bool _isSubmittingTest = false;
  bool _isBleScanning = false;
  DateTime? _lastBleScanAt;
  String? _errorMessage;

  List<NodeTelemetry> get _nodes {
    final nodes = _nodesById.values.toList()
      ..sort((a, b) => a.nodeId.compareTo(b.nodeId));
    return nodes;
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
        _nodesById
          ..clear()
          ..addEntries(nodes.map((node) => MapEntry(node.nodeId, node)));
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
      _nodesById[telemetry.nodeId] = telemetry;
      _backendOnline = true;
      _errorMessage = null;
    });
  }

  Future<void> _startTest(BuildContext context) async {
    final apiService = _apiService;
    if (apiService == null || _isSubmittingTest) {
      return;
    }

    setState(() {
      _isSubmittingTest = true;
    });

    final messenger = ScaffoldMessenger.of(context);
    final selectedLabel = sdacsCaptureLabels.firstWhere(
      (label) => label.id == _selectedCaptureLabelId,
    );
    final requestId =
        'capture_${DateTime.now().toUtc().millisecondsSinceEpoch}';

    try {
      final session = await apiService.startCapture(
        delayMs: _synchronizedStartDelayMs,
        recordSeconds: _testCaptureDurationSeconds,
        label: selectedLabel.id,
        requestId: requestId,
      );
      messenger.showSnackBar(
        SnackBar(
          content: Text(
            '${selectedLabel.displayName} '
            '$_testCaptureDurationSeconds-second capture request sent: '
            '${session.sessionId}',
          ),
        ),
      );
    } on SdacsApiException catch (error) {
      messenger.showSnackBar(
        SnackBar(content: Text('Capture failed: ${error.message}')),
      );
    } finally {
      if (mounted) {
        setState(() {
          _isSubmittingTest = false;
        });
      }
    }
  }

  Future<void> _scanBleNodes(BuildContext context) async {
    final apiService = _apiService;
    if (apiService == null || _isBleScanning) return;
    setState(() => _isBleScanning = true);
    final messenger = ScaffoldMessenger.of(context);
    try {
      final result = await apiService.scanBleNodes();
      if (!mounted || apiService != _apiService) return;
      setState(() {
        _bleResultsByNodeId
          ..clear()
          ..addEntries(result.nodes.map((node) => MapEntry(node.nodeId, node)));
        _lastBleScanAt = result.completedAt;
      });
      final message = result.detectedCount == 0
          ? 'BLE scan complete: no SDACS nodes detected.'
          : 'BLE scan complete: ${result.detectedCount} nodes detected.';
      messenger.showSnackBar(SnackBar(content: Text(message)));
    } on SdacsApiException catch (error) {
      messenger.showSnackBar(
        SnackBar(content: Text('BLE scan failed: ${error.message}')),
      );
    } finally {
      if (mounted) setState(() => _isBleScanning = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final nodes = _nodes;

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
        body: LayoutBuilder(
          builder: (context, constraints) {
            final isWide = constraints.maxWidth >= 1050;

            return SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(24, 10, 24, 32),
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 1320),
                  child: Column(
                    children: [
                      _HeroSection(
                        selectedLabelId: _selectedCaptureLabelId,
                        onLabelChanged: (labelId) {
                          setState(() {
                            _selectedCaptureLabelId = labelId;
                          });
                        },
                        onStartTest: () => _startTest(context),
                        isSubmittingTest: _isSubmittingTest,
                        onScanBle: () => _scanBleNodes(context),
                        isBleScanning: _isBleScanning,
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
                                  child: _RoomMap(
                                    nodes: nodes,
                                    bleResultsByNodeId: _bleResultsByNodeId,
                                    hasBleScan: _lastBleScanAt != null,
                                  ),
                                ),
                                const SizedBox(width: 22),
                                Expanded(
                                  flex: 3,
                                  child: _SystemPanel(
                                    nodes: nodes,
                                    backendOnline: _backendOnline,
                                  ),
                                ),
                              ],
                            )
                          : Column(
                              children: [
                                _RoomMap(
                                  nodes: nodes,
                                  bleResultsByNodeId: _bleResultsByNodeId,
                                  hasBleScan: _lastBleScanAt != null,
                                ),
                                const SizedBox(height: 22),
                                _SystemPanel(
                                  nodes: nodes,
                                  backendOnline: _backendOnline,
                                ),
                              ],
                            ),
                      const SizedBox(height: 28),
                      _LowerSection(nodes: nodes),
                    ],
                  ),
                ),
              ),
            );
          },
        ),
      ),
    );
  }
}

class _HeroSection extends StatelessWidget {
  const _HeroSection({
    required this.selectedLabelId,
    required this.onLabelChanged,
    required this.onStartTest,
    required this.isSubmittingTest,
    required this.onScanBle,
    required this.isBleScanning,
  });

  final String selectedLabelId;
  final ValueChanged<String> onLabelChanged;
  final VoidCallback onStartTest;
  final bool isSubmittingTest;
  final VoidCallback onScanBle;
  final bool isBleScanning;

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
          const SizedBox(height: 24),
          ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 440),
            child: DropdownButtonFormField<String>(
              initialValue: selectedLabelId,
              isExpanded: true,
              dropdownColor: MainScreen.panelLight,
              decoration: InputDecoration(
                labelText: 'Capture label',
                helperText: 'Stored with the next four-node capture.',
                filled: true,
                fillColor: MainScreen.panelLight,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                ),
              ),
              items: sdacsCaptureLabels.map((label) {
                return DropdownMenuItem<String>(
                  value: label.id,
                  child: Text(label.displayName),
                );
              }).toList(),
              onChanged: (value) {
                if (value != null) {
                  onLabelChanged(value);
                }
              },
            ),
          ),
          const SizedBox(height: 28),
          Wrap(
            alignment: WrapAlignment.center,
            spacing: 14,
            runSpacing: 14,
            children: [
              _ActionButton(
                icon: Icons.settings_outlined,
                label: 'Setup',
                filled: true,
                onPressed: () => Navigator.pushNamed(context, AppRoutes.setup),
              ),
              _ActionButton(
                icon: Icons.tune,
                label: 'Calibrate',
                onPressed: () =>
                    Navigator.pushNamed(context, AppRoutes.calibration),
              ),
              _ActionButton(
                icon: Icons.play_arrow_rounded,
                label: 'Test',
                onPressed: isSubmittingTest ? null : onStartTest,
              ),
              _ActionButton(
                icon: Icons.bluetooth_searching,
                label: isBleScanning ? 'Scanning...' : 'Bluetooth RSSI',
                onPressed: isBleScanning ? null : onScanBle,
                showProgress: isBleScanning,
              ),
              _ActionButton(
                icon: Icons.insights,
                label: 'Results',
                onPressed: () => Navigator.pushNamed(context, AppRoutes.graphs),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _ActionButton extends StatelessWidget {
  const _ActionButton({
    required this.icon,
    required this.label,
    required this.onPressed,
    this.filled = false,
    this.showProgress = false,
  });

  final IconData icon;
  final String label;
  final VoidCallback? onPressed;
  final bool filled;
  final bool showProgress;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
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
  }
}

class _RoomMap extends StatelessWidget {
  const _RoomMap({
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
    if (node.dbSpl >= 75) return const Color(0xFFFF6B6B);
    if (node.dbSpl >= 68) return const Color(0xFFFFB86B);
    if (node.dbSpl <= 55) return const Color(0xFFA1A1AA);
    return MainScreen.accentLight;
  }

  String get splStatus {
    if (node.dbSpl >= 75) return 'Very High';
    if (node.dbSpl >= 68) return 'High';
    if (node.dbSpl <= 55) return 'Low';
    return 'Normal';
  }

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message:
          '${node.nodeId}: ${node.dbSpl} dB SPL, ${node.peakFrequencyHz} Hz peak; $bleRssiText',
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
              '${node.dbSpl} dB SPL',
              style: TextStyle(color: splColor, fontWeight: FontWeight.w900),
            ),
            const SizedBox(height: 2),
            Text(
              '${node.peakFrequencyHz} Hz peak',
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
              value: node.batterySoc / 100,
              color: MainScreen.accent,
              backgroundColor: MainScreen.panelLight,
              minHeight: 5,
              borderRadius: BorderRadius.circular(20),
            ),
            const SizedBox(height: 6),
            Text(
              '$splStatus - ${node.batterySoc.toStringAsFixed(0)}% battery',
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
  const _SystemPanel({required this.nodes, required this.backendOnline});

  final List<NodeTelemetry> nodes;
  final bool backendOnline;

  @override
  Widget build(BuildContext context) {
    final avgBattery = nodes.isEmpty
        ? 0
        : nodes.map((n) => n.batterySoc).reduce((a, b) => a + b) / nodes.length;

    final avgSpl = nodes.isEmpty
        ? 0
        : nodes.map((n) => n.dbSpl).reduce((a, b) => a + b) / nodes.length;

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
            label: 'Average SPL',
            value: '${avgSpl.round()} dB',
            icon: Icons.graphic_eq,
          ),
          _MetricTile(
            label: 'Average Battery',
            value: '${avgBattery.round()}%',
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
  const _LowerSection({required this.nodes});

  final List<NodeTelemetry> nodes;

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
            return NodeStatusCard(telemetry: nodes[index]);
          },
        ),
      ],
    );
  }
}
