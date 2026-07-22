import 'dart:async';

import 'package:flutter/material.dart';

import '../../config/backend_config.dart';
import '../../models/capture_session.dart';
import '../../services/sdacs_api_service.dart';
import '../../widgets/sdacs_app_bar.dart';

class GraphsScreen extends StatefulWidget {
  const GraphsScreen({super.key});

  @override
  State<GraphsScreen> createState() => _GraphsScreenState();
}

class _GraphsScreenState extends State<GraphsScreen> {
  Timer? _pollTimer;
  String? _captureId;
  CaptureSession? _session;
  CaptureCombinedResult? _result;
  String? _error;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final captureId = ModalRoute.of(context)?.settings.arguments as String?;
    if (captureId == null || captureId == _captureId) return;
    _captureId = captureId;
    _pollTimer?.cancel();
    unawaited(_refresh());
    _pollTimer = Timer.periodic(
      const Duration(seconds: 3),
      (_) => unawaited(_refresh()),
    );
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    final captureId = _captureId;
    if (captureId == null) return;
    final service = SdacsApiService(
      config: BackendConfigScope.configOf(context),
    );
    try {
      final session = await service.getCapture(captureId);
      final result = session.isTerminal
          ? await service.getCaptureResult(captureId)
          : null;
      if (!mounted || captureId != _captureId) return;
      setState(() {
        _session = session;
        _result = result;
        _error = null;
      });
      if (session.isTerminal) _pollTimer?.cancel();
    } on SdacsApiException catch (error) {
      if (mounted) setState(() => _error = error.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    final session = _session;
    final result = _result;
    return Scaffold(
      appBar: const SdacsAppBar(title: 'Capture Results'),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          if (_captureId == null)
            const Card(
              child: Padding(
                padding: EdgeInsets.all(16),
                child: Text('Run a Test before opening capture results.'),
              ),
            )
          else ...[
            _Section(
              title: 'Capture',
              children: [
                Text('Capture ID: $_captureId'),
                Text('Status: ${session?.status ?? 'Loading…'}'),
                if (session != null) ...[
                  Text('Duration: ${session.durationSeconds} seconds'),
                  Text(
                    'Completed nodes: ${session.completedNodes.isEmpty ? 'None yet' : session.completedNodes.join(', ')}',
                  ),
                  Text(
                    'Missing nodes: ${session.missingNodes.isEmpty ? 'None' : session.missingNodes.join(', ')}',
                  ),
                ],
              ],
            ),
            if (_error != null)
              Text(
                _error!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            if (session != null && !session.isTerminal)
              const LinearProgressIndicator(),
            if (result != null) ...[
              const SizedBox(height: 12),
              _AcousticSection(result: result, captureId: _captureId!),
              const SizedBox(height: 12),
              _EdgeImpulseSection(result: result),
              const SizedBox(height: 12),
              _FusionSection(result: result),
            ],
          ],
        ],
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section({required this.title, required this.children});
  final String title;
  final List<Widget> children;
  @override
  Widget build(BuildContext context) => Card(
    child: Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          ...children,
        ],
      ),
    ),
  );
}

class _AcousticSection extends StatelessWidget {
  const _AcousticSection({required this.result, required this.captureId});
  final CaptureCombinedResult result;
  final String captureId;
  @override
  Widget build(BuildContext context) {
    final data = result.acoustic;
    final service = SdacsApiService(
      config: BackendConfigScope.configOf(context),
    );
    return _Section(
      title: 'Acoustic analysis',
      children: [
        Text('Processor: ${data['processor'] ?? 'Unavailable'}'),
        Text(
          'Nodes used: ${(data['nodes_used'] as List<dynamic>? ?? const []).join(', ')}',
        ),
        Text('Dominant band: ${data['dominant_band'] ?? 'Unavailable'}'),
        Text('Loudest node: ${data['loudest_node_id'] ?? 'Unavailable'}'),
        Text(
          'Spatial variation: ${data['spatial_variation_db'] ?? 'Unavailable'} dB',
        ),
        if (data['plot_filename'] != null)
          Image.network(
            service.capturePlotUri(captureId).toString(),
            key: ValueKey(captureId),
            errorBuilder: (_, _, _) =>
                const Text('Capture-specific plot is unavailable.'),
          ),
      ],
    );
  }
}

class _EdgeImpulseSection extends StatelessWidget {
  const _EdgeImpulseSection({required this.result});
  final CaptureCombinedResult result;
  @override
  Widget build(BuildContext context) {
    final data = result.edgeImpulse;
    final simulated = data['is_simulated'] == true;
    return _Section(
      title: 'Edge Impulse',
      children: [
        Text('Model status: ${data['status'] ?? 'Unavailable'}'),
        if (data['predicted_label'] != null)
          Text('Predicted class: ${data['predicted_label']}'),
        if (data['confidence'] != null)
          Text(
            'Model confidence: ${((data['confidence'] as num) * 100).toStringAsFixed(1)}%',
          ),
        if (data['model_version'] != null)
          Text('Model version: ${data['model_version']}'),
        if (simulated)
          const Text(
            'SIMULATED TEST RESULT',
            style: TextStyle(color: Colors.orange),
          ),
        if (data['status'] == 'model_not_configured' ||
            data['status'] == 'disabled')
          const Text(
            'Acoustic analysis is complete. The final Edge Impulse model is not currently configured.',
          ),
      ],
    );
  }
}

class _FusionSection extends StatelessWidget {
  const _FusionSection({required this.result});
  final CaptureCombinedResult result;
  @override
  Widget build(BuildContext context) {
    final fusion = result.fusion;
    final recommendation = result.recommendation;
    return _Section(
      title: 'Evidence-fused recommendation',
      children: [
        Text('Agreement: ${fusion['agreement'] ?? 'Unavailable'}'),
        Text(
          'Recommendation confidence: ${fusion['recommendation_confidence'] ?? 'Unavailable'}',
        ),
        const SizedBox(height: 8),
        Text(
          recommendation['summary']?.toString() ??
              'No recommendation is available.',
        ),
        for (final evidence
            in (recommendation['evidence'] as List<dynamic>? ?? const []))
          if (evidence is Map) Text('• ${evidence['statement']}'),
      ],
    );
  }
}
