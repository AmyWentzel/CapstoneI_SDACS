import 'dart:async';

import 'package:flutter/material.dart';

import '../../config/backend_config.dart';
import '../../models/capture_session.dart';
import '../../services/sdacs_api_service.dart';
import '../../widgets/sdacs_app_bar.dart';

class CaptureResultsArguments {
  const CaptureResultsArguments({required this.captureId});
  final String captureId;
}

class GraphsScreen extends StatefulWidget {
  const GraphsScreen({
    super.key,
    this.arguments,
    this.service,
    this.plotBuilder,
    this.pollInterval = const Duration(seconds: 3),
  });

  final CaptureResultsArguments? arguments;
  final SdacsApiService? service;
  final Widget Function(Uri uri)? plotBuilder;
  final Duration pollInterval;

  @override
  State<GraphsScreen> createState() => _GraphsScreenState();
}

class _GraphsScreenState extends State<GraphsScreen> {
  Timer? _pollTimer;
  SdacsApiService? _service;
  String? _captureId;
  CaptureSession? _session;
  CaptureCombinedResult? _result;
  String? _error;
  bool _loading = false;
  int _requestCycle = 0;
  int _plotVersion = DateTime.now().millisecondsSinceEpoch;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _service ??=
        widget.service ??
        SdacsApiService(config: BackendConfigScope.configOf(context));
    final routeArgument = ModalRoute.of(context)?.settings.arguments;
    final arguments =
        widget.arguments ??
        (routeArgument is CaptureResultsArguments ? routeArgument : null);
    final candidate = arguments?.captureId.trim();
    if (candidate == _captureId) return;
    _pollTimer?.cancel();
    _captureId = SdacsApiService.isValidCaptureId(candidate) ? candidate : null;
    _session = null;
    _result = null;
    _error = candidate == null || candidate.isEmpty
        ? null
        : 'The capture ID is invalid.';
    if (_captureId != null) unawaited(_refresh());
  }

  @override
  void dispose() {
    _requestCycle++;
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    final service = _service;
    final captureId = _captureId;
    if (service == null || captureId == null || _loading) return;
    final cycle = ++_requestCycle;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final session = await service.getCapture(captureId);
      if (session.captureId != captureId) {
        throw const SdacsApiException(
          'Backend returned a different capture session.',
        );
      }
      if (!_isCurrent(cycle, captureId)) return;
      setState(() => _session = session);
      CaptureCombinedResult? result;
      if (session.isTerminal && session.status != 'failed') {
        result = await service.getCaptureResult(captureId);
        if (result != null && result.captureId != captureId) {
          throw const SdacsApiException(
            'Backend returned a result for a different capture.',
          );
        }
        if (!_isCurrent(cycle, captureId)) return;
      }
      setState(() {
        _session = session;
        _result = result;
        _loading = false;
        _plotVersion = DateTime.now().millisecondsSinceEpoch;
      });
      if (session.isTerminal &&
          (result != null || session.status == 'failed')) {
        _pollTimer?.cancel();
      } else {
        _schedulePoll();
      }
    } on SdacsApiException catch (error) {
      if (!_isCurrent(cycle, captureId)) return;
      setState(() {
        _loading = false;
        _error = error.message;
      });
      if (_session?.isTerminal != true) _schedulePoll();
    }
  }

  bool _isCurrent(int cycle, String captureId) =>
      mounted && cycle == _requestCycle && captureId == _captureId;

  void _schedulePoll() {
    _pollTimer?.cancel();
    _pollTimer = Timer(widget.pollInterval, () => unawaited(_refresh()));
  }

  @override
  Widget build(BuildContext context) {
    final captureId = _captureId;
    final session = _session;
    final result = _result;
    final acoustic = result?.acousticAnalysis;
    final failed =
        session != null &&
        const {
          'failed',
          'processing_failed',
          'collection_failed',
        }.contains(session.status);
    return Scaffold(
      appBar: const SdacsAppBar(title: 'Capture Results'),
      body: RefreshIndicator(
        onRefresh: _refresh,
        child: ListView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: const EdgeInsets.all(16),
          children: [
            if (captureId == null)
              _MessageCard(
                message: _error ?? 'Run a Capture to generate results.',
                isError: _error != null,
              )
            else ...[
              _CaptureSummary(captureId: captureId, session: session),
              if (_loading) ...[
                const SizedBox(height: 12),
                LinearProgressIndicator(borderRadius: BorderRadius.circular(8)),
                const SizedBox(height: 8),
                Text(_progressMessage(session)),
              ],
              if (!_loading && session != null && !session.isTerminal) ...[
                const SizedBox(height: 12),
                Text(_progressMessage(session)),
              ],
              if (!_loading &&
                  session?.isTerminal == true &&
                  !failed &&
                  result == null) ...[
                const SizedBox(height: 12),
                const Text('Generating the capture plot…'),
              ],
              if (_error != null) ...[
                const SizedBox(height: 12),
                _FailureCard(
                  title: 'Results unavailable',
                  reason: _error!,
                  onRefresh: _refresh,
                ),
              ],
              if (failed) ...[
                const SizedBox(height: 12),
                _FailureCard(
                  title: 'Acoustic processing failed',
                  stage: session.failureStage,
                  reason:
                      session.failureReason ??
                      'The backend did not provide a failure reason.',
                  onRefresh: _refresh,
                ),
              ] else if (result != null && acoustic != null) ...[
                const SizedBox(height: 12),
                _StatusRow(session: session!, result: result),
                const SizedBox(height: 12),
                _AiClassificationCard(result: result.edgeImpulseResult),
                const SizedBox(height: 12),
                if (acoustic.isSuccessful && acoustic.plotFilename != null)
                  _CapturePlot(
                    uri: _service!.capturePlotUri(
                      captureId,
                      cacheBust: _plotVersion,
                    ),
                    customBuilder: widget.plotBuilder,
                    onRetry: () => setState(() => _plotVersion++),
                  ),
                const SizedBox(height: 12),
                _Recommendation(result: result),
                const SizedBox(height: 12),
                _RoomSummary(acoustic: acoustic),
                const SizedBox(height: 12),
                _NodeResults(acoustic: acoustic),
                if (_warnings(result).isNotEmpty) ...[
                  const SizedBox(height: 12),
                  _Warnings(warnings: _warnings(result)),
                ],
              ],
            ],
          ],
        ),
      ),
    );
  }

  static String _progressMessage(CaptureSession? session) {
    final stage = session?.processingStage ?? '';
    if (stage.contains('acoustic')) {
      return 'Acoustic processing is running…';
    }
    if (session?.status == 'processing') {
      return 'Generating the capture plot…';
    }
    return 'Waiting for the four nodes to complete the capture…';
  }

  static List<String> _warnings(CaptureCombinedResult result) {
    final values = <String>{
      ...result.acousticAnalysis.warnings,
      ...result.edgeImpulseResult.warnings,
    };
    for (final metric in result.acousticAnalysis.nodeMetrics.values) {
      values.addAll(metric.warnings);
    }
    return values.where((value) => value.trim().isNotEmpty).toList();
  }
}

class _AiClassificationCard extends StatelessWidget {
  const _AiClassificationCard({required this.result});
  final EdgeImpulseResult result;

  static const labels = {
    'speech': 'Speech',
    'quiet_room_white_noise': 'Quiet Room / White Noise',
    'noisy': 'Noisy',
  };

  @override
  Widget build(BuildContext context) {
    if (result.status == 'processing' || result.status == 'pending') {
      return const _Section(
        title: 'AI Room Classification',
        children: [Text('Processing...')],
      );
    }
    if (result.status != 'complete') {
      final disabled = result.status == 'disabled';
      final windowsComplete = result.status == 'fusion_not_configured';
      return _Section(
        title: windowsComplete
            ? 'AI Window Analysis Complete'
            : 'AI Room Classification Unavailable',
        children: [
          Text(
            windowsComplete
                ? 'Capture-level classification requires a validated fusion rule.'
                : disabled
                ? 'Edge Impulse is disabled on the backend.'
                : result.status == 'not_applicable'
                ? 'Calibration captures are not classified.'
                : 'Acoustic results are still available.',
          ),
          if (result.error?.trim().isNotEmpty == true)
            Tooltip(
              message: result.error!,
              child: const Padding(
                padding: EdgeInsets.only(top: 8),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Icon(Icons.info_outline, size: 18),
                    SizedBox(width: 6),
                    Text('AI details'),
                  ],
                ),
              ),
            ),
        ],
      );
    }
    final accepted = result.accepted == true;
    final confidence = result.confidence;
    return _Section(
      title: 'AI Room Classification',
      children: [
        Text(
          accepted ? (result.displayLabel ?? 'Classification') : 'Uncertain',
          style: Theme.of(context).textTheme.headlineSmall,
        ),
        if (confidence != null)
          Text(
            accepted
                ? '${(confidence * 100).round()}% confidence'
                : 'Highest candidate: ${labels[result.predictedLabel] ?? result.predictedLabel ?? 'Unknown'} — ${(confidence * 100).round()}%',
          ),
        const SizedBox(height: 12),
        for (final key in const ['speech', 'quiet_room_white_noise', 'noisy'])
          if (result.scores[key] case final probability?)
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                children: [
                  SizedBox(width: 190, child: Text(labels[key]!)),
                  Expanded(child: LinearProgressIndicator(value: probability)),
                  const SizedBox(width: 8),
                  Text('${(probability * 100).round()}%'),
                ],
              ),
            ),
        if (result.successfulWindowCount != null) ...[
          const SizedBox(height: 4),
          Text(
            'Evidence: ${result.successfulWindowCount} synchronized four-node windows',
          ),
        ],
        if (result.windowLabelCounts.isNotEmpty) ...[
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final key in const [
                'speech',
                'quiet_room_white_noise',
                'noisy',
              ])
                if (result.windowLabelCounts[key] case final count?)
                  Chip(label: Text('${labels[key]}: $count windows')),
            ],
          ),
        ],
        if (result.modelName != null) ...[
          const SizedBox(height: 4),
          Text(
            'Model: ${result.modelName}'
            '${result.modelVersion == null ? '' : ' v${result.modelVersion}'}',
          ),
        ],
      ],
    );
  }
}

class _CaptureSummary extends StatelessWidget {
  const _CaptureSummary({required this.captureId, required this.session});
  final String captureId;
  final CaptureSession? session;

  @override
  Widget build(BuildContext context) => _Section(
    title: 'Capture summary',
    children: [
      _Value('Capture ID', captureId),
      _Value('Collection', _collectionStatus(session)),
      _Value(
        'Completed nodes',
        session == null
            ? 'Loading…'
            : '${session!.completedNodes.length} of 4'
                  '${session!.completedNodes.isEmpty ? '' : ' (${session!.completedNodes.join(', ')})'}',
      ),
      _Value(
        'Missing nodes',
        session == null || session!.missingNodes.isEmpty
            ? 'None'
            : session!.missingNodes.join(', '),
      ),
      if (session?.updatedAt != null)
        _Value('Updated', session!.updatedAt!.toLocal().toString()),
    ],
  );

  static String _collectionStatus(CaptureSession? session) {
    if (session == null) return 'Loading…';
    if (session.completedNodes.length >= 4) return 'Complete';
    if (session.isTerminal && session.completedNodes.isNotEmpty) {
      return 'Partial';
    }
    if (session.status == 'collection_failed') return 'Failed';
    return 'In progress';
  }
}

class _StatusRow extends StatelessWidget {
  const _StatusRow({required this.session, required this.result});
  final CaptureSession session;
  final CaptureCombinedResult result;

  @override
  Widget build(BuildContext context) {
    final acoustic = result.acousticAnalysis;
    final edge = result.edgeImpulseResult;
    final processedRows = acoustic.nodeMetrics.values
        .map((metric) => metric.sampleCount)
        .whereType<int>()
        .fold(0, (total, count) => total + count);
    return _Section(
      title: 'Status',
      children: [
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            _StatusChip(
              label: 'Collection',
              value: session.missingNodes.isEmpty ? 'Complete' : 'Partial',
            ),
            _StatusChip(
              label: 'Acoustic processing',
              value: acoustic.status == 'partial'
                  ? 'Partial'
                  : acoustic.isSuccessful
                  ? 'Complete'
                  : 'Failed',
            ),
            _StatusChip(
              label: 'Edge Impulse',
              value: edge.isUnavailable ? 'Disabled' : edge.status,
            ),
          ],
        ),
        if (edge.isUnavailable)
          const Padding(
            padding: EdgeInsets.only(top: 8),
            child: Text('Final model not configured'),
          ),
        if (processedRows > 0)
          Padding(
            padding: const EdgeInsets.only(top: 8),
            child: Text('$processedRows feature rows processed'),
          ),
      ],
    );
  }
}

class _CapturePlot extends StatefulWidget {
  const _CapturePlot({
    required this.uri,
    required this.onRetry,
    this.customBuilder,
  });
  final Uri uri;
  final VoidCallback onRetry;
  final Widget Function(Uri uri)? customBuilder;

  @override
  State<_CapturePlot> createState() => _CapturePlotState();
}

class _CapturePlotState extends State<_CapturePlot> {
  void _openFullScreen() {
    showDialog<void>(
      context: context,
      barrierColor: Colors.black87,
      builder: (_) => _FullScreenPlot(
        key: ValueKey('fullscreen-${widget.uri}'),
        uri: widget.uri,
        customBuilder: widget.customBuilder,
      ),
    );
  }

  @override
  Widget build(BuildContext context) => _Section(
    title: 'Spatial Acoustic Profile',
    trailing: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        IconButton(
          tooltip: 'Refresh plot',
          onPressed: widget.onRetry,
          icon: const Icon(Icons.refresh),
        ),
        IconButton(
          tooltip: 'Expand plot',
          onPressed: _openFullScreen,
          icon: const Icon(Icons.fullscreen),
        ),
      ],
    ),
    children: [
      const Text(
        'Node position from saved room layout. Band wedges show low/mid/high '
        'energy ratios. Halo represents Estimated SPL.',
      ),
      const SizedBox(height: 10),
      LayoutBuilder(
        builder: (context, constraints) => ClipRect(
          child: SizedBox(
            key: const ValueKey('inline-capture-plot'),
            width: double.infinity,
            height: (constraints.maxWidth * 0.55).clamp(280.0, 460.0),
            child:
                widget.customBuilder?.call(widget.uri) ??
                Image.network(
                  widget.uri.toString(),
                  key: ValueKey(widget.uri),
                  fit: BoxFit.contain,
                  loadingBuilder: (context, child, progress) => progress == null
                      ? child
                      : const Center(
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              CircularProgressIndicator(),
                              SizedBox(height: 10),
                              Text('Loading capture plot…'),
                            ],
                          ),
                        ),
                  errorBuilder: (_, _, _) => Center(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        const Text('Capture plot could not be loaded.'),
                        TextButton(
                          onPressed: widget.onRetry,
                          child: const Text('Retry'),
                        ),
                      ],
                    ),
                  ),
                ),
          ),
        ),
      ),
    ],
  );
}

class _FullScreenPlot extends StatefulWidget {
  const _FullScreenPlot({
    super.key,
    required this.uri,
    required this.customBuilder,
  });
  final Uri uri;
  final Widget Function(Uri uri)? customBuilder;

  @override
  State<_FullScreenPlot> createState() => _FullScreenPlotState();
}

class _FullScreenPlotState extends State<_FullScreenPlot> {
  late final TransformationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = TransformationController();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  Widget _image() =>
      widget.customBuilder?.call(widget.uri) ??
      Image.network(
        widget.uri.toString(),
        key: ValueKey('fullscreen-image-${widget.uri}'),
        fit: BoxFit.contain,
        loadingBuilder: (context, child, progress) => progress == null
            ? child
            : const Center(child: CircularProgressIndicator()),
        errorBuilder: (_, _, _) =>
            const Center(child: Text('Capture plot could not be loaded.')),
      );

  @override
  Widget build(BuildContext context) => Dialog.fullscreen(
    backgroundColor: const Color(0xFF08080C),
    child: SafeArea(
      child: Column(
        children: [
          AppBar(
            title: const Text('Spatial Acoustic Profile'),
            automaticallyImplyLeading: false,
            actions: [
              IconButton(
                tooltip: 'Reset view',
                onPressed: () => _controller.value = Matrix4.identity(),
                icon: const Icon(Icons.center_focus_strong),
              ),
              IconButton(
                tooltip: 'Close',
                onPressed: () => Navigator.pop(context),
                icon: const Icon(Icons.close),
              ),
            ],
          ),
          Expanded(
            child: ClipRect(
              child: InteractiveViewer(
                key: const ValueKey('fullscreen-capture-viewer'),
                transformationController: _controller,
                minScale: 0.8,
                maxScale: 5,
                trackpadScrollCausesScale: false,
                child: SizedBox.expand(child: Center(child: _image())),
              ),
            ),
          ),
        ],
      ),
    ),
  );
}

class _RoomSummary extends StatelessWidget {
  const _RoomSummary({required this.acoustic});
  final AcousticAnalysis acoustic;

  @override
  Widget build(BuildContext context) {
    if (!acoustic.isSuccessful) {
      return const _MessageCard(
        message: 'No usable acoustic summary is available.',
        isError: true,
      );
    }
    return _Section(
      title: 'Room summary',
      children: [
        _Value(
          'Nodes used',
          acoustic.nodesUsed.isEmpty
              ? 'Unavailable'
              : acoustic.nodesUsed.join(', '),
        ),
        _Value('Mean estimated SPL', _db(acoustic.meanEstimatedSplDb)),
        _Value('Dominant band', acoustic.dominantBand ?? 'Unavailable'),
        _Value('Dominant frequency', _hz(acoustic.dominantFrequencyHz)),
        _Value('Loudest node', acoustic.loudestNodeId ?? 'Unavailable'),
        if (acoustic.quietestNodeId != null)
          _Value('Quietest node', acoustic.quietestNodeId!),
        _Value('Spatial variation', _db(acoustic.spatialVariationDb)),
        if (acoustic.status == 'partial' || acoustic.missingNodes.isNotEmpty)
          Text(
            'Partial data: missing ${acoustic.missingNodes.join(', ')}',
            style: const TextStyle(color: Colors.amber),
          ),
      ],
    );
  }
}

class _NodeResults extends StatelessWidget {
  const _NodeResults({required this.acoustic});
  final AcousticAnalysis acoustic;

  @override
  Widget build(BuildContext context) {
    final entries = acoustic.nodeMetrics.entries.toList()
      ..sort((a, b) => a.key.compareTo(b.key));
    return _Section(
      title: 'Node results',
      children: entries.isEmpty
          ? const [Text('No node metrics are available.')]
          : [
              LayoutBuilder(
                builder: (context, constraints) => Wrap(
                  spacing: 10,
                  runSpacing: 10,
                  children: [
                    for (final entry in entries)
                      SizedBox(
                        width: constraints.maxWidth >= 700
                            ? (constraints.maxWidth - 30) / 4
                            : constraints.maxWidth >= 420
                            ? (constraints.maxWidth - 10) / 2
                            : constraints.maxWidth,
                        child: Card(
                          color: Theme.of(context).colorScheme.surfaceContainer,
                          child: Padding(
                            padding: const EdgeInsets.all(12),
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  entry.key,
                                  style: const TextStyle(
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                                Text(
                                  'Estimated SPL: ${_db(entry.value.estimatedSplDb)}',
                                ),
                                Text(
                                  'Representative frequency: ${_hz(entry.value.peakFrequencyHz)}',
                                ),
                                if ([
                                  entry.value.lowRatio,
                                  entry.value.midRatio,
                                  entry.value.highRatio,
                                ].every(
                                  (value) => value != null && value.isFinite,
                                ))
                                  Wrap(
                                    spacing: 12,
                                    runSpacing: 2,
                                    children: [
                                      _BandRatio(
                                        label: 'L',
                                        value: entry.value.lowRatio!,
                                        color: const Color(0xFF3B82F6),
                                      ),
                                      _BandRatio(
                                        label: 'M',
                                        value: entry.value.midRatio!,
                                        color: const Color(0xFF22C55E),
                                      ),
                                      _BandRatio(
                                        label: 'H',
                                        value: entry.value.highRatio!,
                                        color: const Color(0xFFEF4444),
                                      ),
                                    ],
                                  )
                                else
                                  const Text('Band ratios: Unavailable'),
                                Text(
                                  'Samples: ${entry.value.sampleCount ?? '—'}',
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
              ),
            ],
    );
  }
}

class _BandRatio extends StatelessWidget {
  const _BandRatio({
    required this.label,
    required this.value,
    required this.color,
  });
  final String label;
  final double value;
  final Color color;

  @override
  Widget build(BuildContext context) => Text(
    '$label ${_ratio(value)}',
    style: TextStyle(color: color, fontWeight: FontWeight.w600),
  );
}

class _Warnings extends StatelessWidget {
  const _Warnings({required this.warnings});
  final List<String> warnings;

  @override
  Widget build(BuildContext context) => _Section(
    title: 'Warnings',
    children: [
      for (final warning in warnings)
        ListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          leading: const Icon(Icons.warning_amber, color: Colors.amber),
          title: Text(warning),
        ),
    ],
  );
}

class _Recommendation extends StatelessWidget {
  const _Recommendation({required this.result});
  final CaptureCombinedResult result;

  static const _labels = {
    'speech': 'Speech',
    'quiet_room_white_noise': 'Quiet Room / White Noise',
    'noisy': 'Noisy',
  };

  @override
  Widget build(BuildContext context) {
    final acoustic = result.acousticAnalysis;
    if (!acoustic.isSuccessful) return const SizedBox.shrink();
    final plan = _buildPlan(result);
    final backendSummary = result.recommendation['summary']?.toString().trim();

    return _Section(
      title: 'Recommendations & Spatial Interpretation',
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(plan.icon, size: 28),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    plan.headline,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  const SizedBox(height: 4),
                  Text(plan.interpretation),
                ],
              ),
            ),
          ],
        ),
        const SizedBox(height: 14),
        Text(
          'Recommended next action',
          style: Theme.of(context).textTheme.titleSmall,
        ),
        const SizedBox(height: 4),
        Text(plan.primaryAction),
        if (plan.secondaryActions.isNotEmpty) ...[
          const SizedBox(height: 8),
          for (final action in plan.secondaryActions)
            _EvidenceLine(icon: Icons.arrow_right, text: action),
        ],
        const Divider(height: 28),
        Text('Evidence used', style: Theme.of(context).textTheme.titleSmall),
        const SizedBox(height: 6),
        for (final item in plan.evidence)
          _EvidenceLine(icon: Icons.check_circle_outline, text: item),
        const SizedBox(height: 10),
        DecoratedBox(
          decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(10),
          ),
          child: const Padding(
            padding: EdgeInsets.all(12),
            child: Text(
              'Spatial-map note: use the acoustic profile above to compare relative node levels and frequency-band energy. The map supports room-level interpretation, but it should not be treated as exact source localization.',
            ),
          ),
        ),
        if (backendSummary?.isNotEmpty == true &&
            backendSummary != plan.interpretation) ...[
          const SizedBox(height: 12),
          ExpansionTile(
            tilePadding: EdgeInsets.zero,
            childrenPadding: EdgeInsets.zero,
            title: const Text('Backend analysis summary'),
            children: [
              Align(
                alignment: Alignment.centerLeft,
                child: Text(backendSummary!),
              ),
            ],
          ),
        ],
      ],
    );
  }

  static _RecommendationPlan _buildPlan(CaptureCombinedResult result) {
    final ai = result.edgeImpulseResult;
    final acoustic = result.acousticAnalysis;
    final label = ai.predictedLabel;
    final displayLabel = _labels[label] ?? label ?? 'Unknown';
    final confidence = ai.confidence;
    final confidenceText = confidence == null
        ? 'an unavailable confidence score'
        : '${(confidence * 100).toStringAsFixed(1)}% confidence';
    final loudest = acoustic.loudestNodeId;
    final variation = acoustic.spatialVariationDb;
    final dominantBand = acoustic.dominantBand;
    final dominantFrequency = acoustic.dominantFrequencyHz;
    final evidence = <String>[];

    if (ai.status == 'complete') {
      evidence.add(
        '${ai.accepted == true ? 'Accepted' : 'Leading'} AI class: $displayLabel at $confidenceText.',
      );
    } else {
      evidence.add('AI classification status: ${ai.status}.');
    }
    if (ai.successfulWindowCount != null) {
      evidence.add(
        '${ai.successfulWindowCount} synchronized four-node windows contributed to the capture result.',
      );
    }
    if (ai.windowLabelCounts.isNotEmpty) {
      final counts = <String>[];
      for (final key in const ['speech', 'quiet_room_white_noise', 'noisy']) {
        final count = ai.windowLabelCounts[key];
        if (count != null) counts.add('${_labels[key]} $count');
      }
      if (counts.isNotEmpty) {
        evidence.add('Window winners: ${counts.join(', ')}.');
      }
    }
    if (loudest != null) {
      evidence.add(
        variation != null && variation.isFinite
            ? '$loudest was the loudest node; spatial variation was ${variation.toStringAsFixed(1)} dB.'
            : '$loudest was the loudest measured node.',
      );
    }
    if (dominantBand != null || dominantFrequency != null) {
      final bandText = dominantBand == null ? '' : '$dominantBand-band';
      final frequencyText = dominantFrequency == null
          ? ''
          : _hz(dominantFrequency);
      evidence.add(
        'Dominant acoustic evidence: ${[bandText, frequencyText].where((value) => value.isNotEmpty).join(' near ')}.',
      );
    }

    if (ai.status != 'complete') {
      return _RecommendationPlan(
        icon: Icons.sync_problem,
        headline: 'Complete a valid four-node AI capture before acting.',
        interpretation:
            'The spatial acoustic profile is available, but the classifier did not return a completed room-level result.',
        primaryAction:
            'Verify that node01 through node04 all publish the same capture ID and sample indexes, then repeat the capture.',
        secondaryActions: const [
          'Keep the room layout and source position unchanged during the repeat test.',
          'Review the missing-node or excluded-window reason before comparing results.',
        ],
        evidence: evidence,
      );
    }

    if (ai.accepted != true) {
      return _RecommendationPlan(
        icon: Icons.help_outline,
        headline: 'Treat this capture as uncertain.',
        interpretation:
            '$displayLabel was the leading candidate at $confidenceText, below the ${(100 * (ai.threshold ?? 0.6)).round()}% acceptance threshold.',
        primaryAction:
            'Repeat a 60-second capture with a sustained, clearly controlled sound source and minimal silent intervals.',
        secondaryActions: const [
          'Compare the new probability distribution with this capture rather than lowering the threshold immediately.',
          'Use the spatial map to check whether one node is dominating or whether the response is distributed across the room.',
        ],
        evidence: evidence,
      );
    }

    if (label == 'speech') {
      return _RecommendationPlan(
        icon: Icons.record_voice_over,
        headline: 'Speech activity was detected.',
        interpretation:
            'The classifier accepted speech at $confidenceText. Use the spatial map to identify the strongest measured region and determine whether speech energy is localized or distributed.',
        primaryAction: loudest == null
            ? 'Review the spatial acoustic map and identify the node with the strongest measured level.'
            : 'Inspect the area represented by $loudest first; it had the strongest measured acoustic level.',
        secondaryActions: const [
          'For speech-privacy or room-treatment testing, repeat the capture after changing source position or adding absorption and compare the map.',
          'Use continuous speech with limited pauses when validating the classifier because silent windows can reduce capture-level confidence.',
        ],
        evidence: evidence,
      );
    }

    if (label == 'noisy') {
      return _RecommendationPlan(
        icon: Icons.volume_up,
        headline: 'A sustained noisy condition was detected.',
        interpretation:
            'The classifier accepted the noisy class at $confidenceText. The spatial profile can be used to prioritize the region with the strongest measured contribution.',
        primaryAction: loudest == null
            ? 'Inspect the room for continuous equipment, ventilation, or broadband noise sources and repeat the capture after isolation.'
            : 'Investigate equipment, ventilation, or other continuous sources nearest the region represented by $loudest.',
        secondaryActions: const [
          'Use the dominant band and frequency as supporting evidence when identifying the source.',
          'Repeat the same capture after mitigation and compare confidence, node levels, and spatial variation.',
        ],
        evidence: evidence,
      );
    }

    return _RecommendationPlan(
      icon: Icons.nights_stay_outlined,
      headline: 'The room was classified as quiet / white noise.',
      interpretation:
          'The classifier accepted the quiet-room class at $confidenceText. This capture can serve as a reference condition for later speech and noisy-room comparisons.',
      primaryAction:
          'Save this room layout and capture as the baseline, then keep node positions fixed during comparative tests.',
      secondaryActions: const [
        'Investigate any node that remains noticeably louder than the others before treating the room as uniformly quiet.',
        'Repeat the baseline periodically after firmware, gain, calibration, or room-layout changes.',
      ],
      evidence: evidence,
    );
  }
}

class _RecommendationPlan {
  const _RecommendationPlan({
    required this.icon,
    required this.headline,
    required this.interpretation,
    required this.primaryAction,
    required this.secondaryActions,
    required this.evidence,
  });

  final IconData icon;
  final String headline;
  final String interpretation;
  final String primaryAction;
  final List<String> secondaryActions;
  final List<String> evidence;
}

class _EvidenceLine extends StatelessWidget {
  const _EvidenceLine({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 6),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(icon, size: 18),
        const SizedBox(width: 7),
        Expanded(child: Text(text)),
      ],
    ),
  );
}

class _FailureCard extends StatelessWidget {
  const _FailureCard({
    required this.title,
    required this.reason,
    required this.onRefresh,
    this.stage,
  });
  final String title;
  final String? stage;
  final String reason;
  final VoidCallback onRefresh;

  @override
  Widget build(BuildContext context) => _Section(
    title: title,
    children: [
      const _Value('Acoustic processing', 'Failed'),
      if (stage != null) _Value('Failure stage', stage!),
      _Value('Reason', reason),
      const SizedBox(height: 8),
      OutlinedButton.icon(
        onPressed: onRefresh,
        icon: const Icon(Icons.refresh),
        label: const Text('Refresh'),
      ),
    ],
  );
}

class _MessageCard extends StatelessWidget {
  const _MessageCard({required this.message, required this.isError});
  final String message;
  final bool isError;

  @override
  Widget build(BuildContext context) => Card(
    child: Padding(
      padding: const EdgeInsets.all(20),
      child: Text(
        message,
        style: TextStyle(
          color: isError ? Theme.of(context).colorScheme.error : null,
        ),
      ),
    ),
  );
}

class _Section extends StatelessWidget {
  const _Section({required this.title, required this.children, this.trailing});
  final String title;
  final List<Widget> children;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) => Card(
    child: Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  title,
                  style: Theme.of(context).textTheme.titleLarge,
                ),
              ),
              ?trailing,
            ],
          ),
          const SizedBox(height: 10),
          ...children,
        ],
      ),
    ),
  );
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Chip(label: Text('$label: $value'));
}

class _Value extends StatelessWidget {
  const _Value(this.label, this.value);
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 5),
    child: Text('$label: $value'),
  );
}

String _db(double? value) =>
    value == null || !value.isFinite ? '—' : '${value.toStringAsFixed(1)} dB';

String _hz(double? value) {
  if (value == null || !value.isFinite) return '—';
  return value >= 1000
      ? '${(value / 1000).toStringAsFixed(2)} kHz'
      : '${value.toStringAsFixed(1)} Hz';
}

String _ratio(double? value) => value == null || !value.isFinite
    ? '—'
    : '${(value * 100).toStringAsFixed(0)}%';
