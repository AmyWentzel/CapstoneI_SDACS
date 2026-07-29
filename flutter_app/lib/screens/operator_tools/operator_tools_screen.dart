import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../models/capture_node_metric.dart';
import '../../models/capture_session.dart';
import '../../services/calibration_audio_service.dart';
import '../../services/calibration_capture_coordinator.dart';
import '../../services/sdacs_api_service.dart';
import '../../state/operation_controller.dart';
import '../main/main_screen.dart';

class OperatorToolsScreen extends StatefulWidget {
  const OperatorToolsScreen({
    super.key,
    this.audioService,
    this.operationController,
    this.coordinator,
  });

  final CalibrationAudio? audioService;
  final OperationController? operationController;
  final CalibrationCaptureCoordinator? coordinator;

  @override
  State<OperatorToolsScreen> createState() => _OperatorToolsScreenState();
}

class _OperatorToolsScreenState extends State<OperatorToolsScreen> {
  static const signalDuration = Duration(seconds: 60);
  static const minimumReferenceSpl = 30.0;
  static const maximumReferenceSpl = 140.0;

  late final CalibrationAudio _audio;
  late final OperationController _operations;
  late final bool _ownsAudio;
  CalibrationCaptureCoordinator? _coordinator;
  StreamSubscription<Duration>? _positionSubscription;
  StreamSubscription<Duration?>? _durationSubscription;
  StreamSubscription<CalibrationPlaybackState>? _stateSubscription;
  final _referenceSplController = TextEditingController();
  Duration _previewPosition = Duration.zero;
  Duration _previewDuration = signalDuration;
  bool _starting = false;
  bool _offsetsCalculated = false;
  String? _referenceError;

  bool get _isPreview => _operations.operation == ActiveOperation.audioPreview;
  bool get _isCalibrationCapture => OperationAccess(
    operation: _operations.operation,
    hasCompletedCapture: false,
    layoutDirty: false,
  ).isCalibrationCapture;
  bool get _isBlocking => _operations.operation != ActiveOperation.idle;

  @override
  void initState() {
    super.initState();
    _audio = widget.audioService ?? CalibrationAudioService();
    _ownsAudio = widget.audioService == null;
    _operations = widget.operationController ?? OperationController.instance;
    _coordinator = widget.coordinator;
    _operations.addListener(_operationChanged);
    _positionSubscription = _audio.positionStream.listen((position) {
      if (mounted && _isPreview) {
        setState(() => _previewPosition = position);
      }
    });
    _durationSubscription = _audio.durationStream.listen((duration) {
      if (mounted && _isPreview && duration != null) {
        setState(() => _previewDuration = duration);
      }
    });
    _stateSubscription = _audio.playerStateStream.listen((state) {
      if (state == CalibrationPlaybackState.completed && _isPreview) {
        _finishPreview();
      }
    });
  }

  void _operationChanged() {
    if (mounted) setState(() {});
  }

  CalibrationCaptureCoordinator _captureCoordinator() {
    return _coordinator ??= CalibrationCaptureCoordinator(
      api: SdacsApiService(config: BackendConfigScope.configOf(context)),
      audio: _audio,
      operations: _operations,
    );
  }

  Future<void> _preview(CalibrationSignal signal) async {
    if (_starting || _isBlocking) return;
    if (!_operations.begin(ActiveOperation.audioPreview, signal: signal)) {
      return;
    }
    setState(() {
      _starting = true;
      _previewPosition = Duration.zero;
      _previewDuration = signalDuration;
    });
    try {
      await _audio.initialize();
      await _audio.preload(signal);
      await _audio.play(signal);
    } catch (error) {
      _operations.finish();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Unable to preview calibration signal: $error'),
          ),
        );
      }
    } finally {
      if (mounted) setState(() => _starting = false);
    }
  }

  Future<void> _runCalibration(CalibrationSignal signal) async {
    if (_starting || _isBlocking) return;
    setState(() => _starting = true);
    try {
      final session = await _captureCoordinator().start(signal);
      if (session != null && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              '60-second calibration capture started: ${session.captureId}',
            ),
          ),
        );
      }
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('$error')));
      }
    } finally {
      if (mounted) setState(() => _starting = false);
    }
  }

  Future<void> _stopPreview() async {
    await _audio.stop();
    _finishPreview();
  }

  void _finishPreview() {
    if (_isPreview) _operations.finish();
    if (mounted) {
      setState(() {
        _previewPosition = Duration.zero;
        _previewDuration = signalDuration;
        _starting = false;
      });
    }
  }

  Future<void> _handleBack() async {
    if (_isCalibrationCapture) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Calibration capture cannot be cancelled safely after it begins.',
          ),
        ),
      );
      return;
    }
    if (!_isPreview) {
      Navigator.of(context).pop();
      return;
    }
    final leave = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Stop calibration signal?'),
        content: const Text(
          'Stop the calibration signal and leave Operator Tools?',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Stay'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Stop and Leave'),
          ),
        ],
      ),
    );
    if (leave == true && mounted) {
      await _stopPreview();
      if (mounted) Navigator.of(context).pop();
    }
  }

  void _calculateOffsets() {
    final reference = double.tryParse(_referenceSplController.text.trim());
    if (reference == null ||
        !reference.isFinite ||
        reference < minimumReferenceSpl ||
        reference > maximumReferenceSpl) {
      setState(() {
        _referenceError =
            'Enter a finite reference SPL from $minimumReferenceSpl to $maximumReferenceSpl dB.';
        _offsetsCalculated = false;
      });
      return;
    }
    setState(() {
      _referenceError = null;
      _offsetsCalculated = true;
    });
  }

  @override
  void dispose() {
    _operations.removeListener(_operationChanged);
    unawaited(_positionSubscription?.cancel());
    unawaited(_durationSubscription?.cancel());
    unawaited(_stateSubscription?.cancel());
    _referenceSplController.dispose();
    if (_isPreview) {
      unawaited(_audio.stop());
      _operations.finish();
    }
    if (_ownsAudio) {
      unawaited(_coordinator?.dispose());
      unawaited(_audio.dispose());
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final activeSignal = _operations.activeSignal;
    final previewRemaining = _previewDuration - _previewPosition;
    final safePreviewRemaining = previewRemaining.isNegative
        ? Duration.zero
        : previewRemaining;
    final previewProgress = _previewDuration.inMilliseconds <= 0
        ? 0.0
        : (_previewPosition.inMilliseconds / _previewDuration.inMilliseconds)
              .clamp(0.0, 1.0);
    final calibrationRemaining =
        _operations.playbackDuration - _operations.playbackPosition;
    final phase = switch (_operations.operation) {
      ActiveOperation.calibrationAudioPreparing ||
      ActiveOperation.calibrationCaptureScheduling => CapturePhase.scheduling,
      ActiveOperation.calibrationCapturing => CapturePhase.capturing,
      ActiveOperation.calibrationProcessing => CapturePhase.processing,
      _ => CapturePhase.idle,
    };

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
      child: PopScope(
        canPop: !_isBlocking,
        onPopInvokedWithResult: (didPop, _) {
          if (!didPop) unawaited(_handleBack());
        },
        child: Scaffold(
          appBar: AppBar(
            title: const Text('Operator Tools'),
            leading: IconButton(
              tooltip: 'Back',
              onPressed: _handleBack,
              icon: const Icon(Icons.arrow_back),
            ),
          ),
          body: Stack(
            children: [
              SingleChildScrollView(
                padding: const EdgeInsets.fromLTRB(20, 16, 20, 36),
                child: Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 1100),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        _ToolSection(
                          title: 'Room and Node Setup',
                          icon: Icons.meeting_room_outlined,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text(
                                'Edit room dimensions, node positions, Test Source position, and saved layout.',
                              ),
                              const SizedBox(height: 14),
                              FilledButton.icon(
                                key: const Key('open-room-setup'),
                                onPressed: _isBlocking
                                    ? null
                                    : () => Navigator.pushNamed(
                                        context,
                                        AppRoutes.setup,
                                      ),
                                icon: const Icon(
                                  Icons.edit_location_alt_outlined,
                                ),
                                label: const Text('Open Room and Node Setup'),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(height: 18),
                        _ToolSection(
                          title: 'Calibration Signals',
                          icon: Icons.surround_sound_outlined,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [
                              LayoutBuilder(
                                builder: (context, constraints) {
                                  final cards = [
                                    _SignalCard(
                                      signal: CalibrationSignal.oneKhzTone,
                                      title: '1 kHz Reference Tone',
                                      description:
                                          'Use a reference sound-level meter during this capture.',
                                      runLabel: 'Run 1 kHz Calibration Capture',
                                      activeSignal: activeSignal,
                                      isPreview: _isPreview,
                                      isBlocked: _isBlocking,
                                      isStarting: _starting,
                                      onPreview: () => _preview(
                                        CalibrationSignal.oneKhzTone,
                                      ),
                                      onRun: () => _runCalibration(
                                        CalibrationSignal.oneKhzTone,
                                      ),
                                      onStop: _stopPreview,
                                    ),
                                    _SignalCard(
                                      signal: CalibrationSignal.frequencySweep,
                                      title: '20 Hz–20 kHz Logarithmic Sweep',
                                      description:
                                          'Use this capture to examine frequency response across the room.',
                                      runLabel: 'Run Sweep Calibration Capture',
                                      activeSignal: activeSignal,
                                      isPreview: _isPreview,
                                      isBlocked: _isBlocking,
                                      isStarting: _starting,
                                      onPreview: () => _preview(
                                        CalibrationSignal.frequencySweep,
                                      ),
                                      onRun: () => _runCalibration(
                                        CalibrationSignal.frequencySweep,
                                      ),
                                      onStop: _stopPreview,
                                    ),
                                  ];
                                  if (constraints.maxWidth >= 760) {
                                    return Row(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.start,
                                      children: [
                                        Expanded(child: cards.first),
                                        const SizedBox(width: 16),
                                        Expanded(child: cards.last),
                                      ],
                                    );
                                  }
                                  return Column(
                                    children: [
                                      cards.first,
                                      const SizedBox(height: 16),
                                      cards.last,
                                    ],
                                  );
                                },
                              ),
                              if (_isPreview) ...[
                                const SizedBox(height: 18),
                                _PlaybackProgress(
                                  signal: activeSignal,
                                  position: _previewPosition,
                                  duration: _previewDuration,
                                  remaining: safePreviewRemaining,
                                  progress: previewProgress,
                                  onStop: _stopPreview,
                                ),
                              ],
                            ],
                          ),
                        ),
                        const SizedBox(height: 18),
                        _SplCalibrationSection(
                          result:
                              _operations.latestCalibrationSignal ==
                                  CalibrationSignal.oneKhzTone
                              ? _operations.latestCalibrationResult
                              : null,
                          referenceController: _referenceSplController,
                          referenceError: _referenceError,
                          offsetsCalculated: _offsetsCalculated,
                          onCalculate: _calculateOffsets,
                        ),
                        const SizedBox(height: 18),
                        _ToolSection(
                          title: 'Advanced Calibration',
                          icon: Icons.tune,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text(
                                'The existing calibration screen is a placeholder; a complete advanced workflow is not configured.',
                              ),
                              const SizedBox(height: 14),
                              OutlinedButton.icon(
                                key: const Key('open-calibration'),
                                onPressed: _isBlocking
                                    ? null
                                    : () => Navigator.pushNamed(
                                        context,
                                        AppRoutes.calibration,
                                      ),
                                icon: const Icon(Icons.science_outlined),
                                label: const Text(
                                  'Open Existing Calibration Controls',
                                ),
                              ),
                            ],
                          ),
                        ),
                        if (_operations.calibrationWarning != null) ...[
                          const SizedBox(height: 18),
                          Text(
                            _operations.calibrationWarning!,
                            style: const TextStyle(color: Colors.amberAccent),
                          ),
                        ],
                      ],
                    ),
                  ),
                ),
              ),
              if (_isCalibrationCapture) ...[
                const Positioned.fill(
                  child: ModalBarrier(
                    dismissible: false,
                    color: Color(0xCC08080C),
                  ),
                ),
                Positioned.fill(
                  child: CaptureProgressOverlay(
                    phase: phase,
                    captureId: _operations.activeCaptureId,
                    totalDuration: signalDuration,
                    remainingDuration: calibrationRemaining.isNegative
                        ? Duration.zero
                        : calibrationRemaining,
                    calibrationSignal: _signalName(activeSignal),
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

String _signalName(CalibrationSignal signal) => switch (signal) {
  CalibrationSignal.oneKhzTone => '1 kHz Reference Tone',
  CalibrationSignal.frequencySweep => '20 Hz–20 kHz Logarithmic Sweep',
  CalibrationSignal.none => 'Calibration Signal',
};

double? suggestedSplOffset({
  required double? currentOffsetDb,
  required double referenceSplDb,
  required double? measuredSplDb,
}) {
  if (currentOffsetDb == null || measuredSplDb == null) return null;
  return currentOffsetDb + referenceSplDb - measuredSplDb;
}

class _SplCalibrationSection extends StatelessWidget {
  const _SplCalibrationSection({
    required this.result,
    required this.referenceController,
    required this.referenceError,
    required this.offsetsCalculated,
    required this.onCalculate,
  });

  final CaptureCombinedResult? result;
  final TextEditingController referenceController;
  final String? referenceError;
  final bool offsetsCalculated;
  final VoidCallback onCalculate;

  @override
  Widget build(BuildContext context) {
    final metrics = result?.nodeMetrics ?? const <String, CaptureNodeMetric>{};
    final reference = double.tryParse(referenceController.text.trim());
    return _ToolSection(
      title: 'SPL Calibration',
      icon: Icons.speed_outlined,
      child: metrics.isEmpty
          ? const Text(
              'Run a 1 kHz Calibration Capture to calculate suggested node offsets.',
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const Text(
                  'SPL Calibration Result',
                  style: TextStyle(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 12),
                TextField(
                  key: const Key('reference-spl-input'),
                  controller: referenceController,
                  keyboardType: const TextInputType.numberWithOptions(
                    decimal: true,
                  ),
                  decoration: InputDecoration(
                    labelText: 'Reference SPL measured at the node (dB SPL)',
                    errorText: referenceError,
                  ),
                ),
                const SizedBox(height: 12),
                OutlinedButton(
                  key: const Key('calculate-offsets'),
                  onPressed: onCalculate,
                  child: const Text('Calculate Suggested Offsets'),
                ),
                if (offsetsCalculated && reference != null) ...[
                  const SizedBox(height: 16),
                  ...metrics.entries.map(
                    (entry) => _OffsetRow(
                      nodeId: entry.key,
                      measured: entry.value.estimatedSplDb,
                      currentOffset: entry.value.currentOffsetDb,
                      reference: reference,
                    ),
                  ),
                ],
                const SizedBox(height: 14),
                const Text(
                  'Applying SPL offsets requires backend or firmware support.',
                  style: TextStyle(color: Colors.amberAccent),
                ),
                const SizedBox(height: 8),
                const FilledButton(
                  onPressed: null,
                  child: Text('Apply Confirmed Offsets'),
                ),
              ],
            ),
    );
  }
}

class _OffsetRow extends StatelessWidget {
  const _OffsetRow({
    required this.nodeId,
    required this.measured,
    required this.currentOffset,
    required this.reference,
  });

  final String nodeId;
  final double? measured;
  final double? currentOffset;
  final double reference;

  @override
  Widget build(BuildContext context) {
    final suggested = suggestedSplOffset(
      currentOffsetDb: currentOffset,
      referenceSplDb: reference,
      measuredSplDb: measured,
    );
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Wrap(
        spacing: 16,
        runSpacing: 4,
        children: [
          Text(nodeId, style: const TextStyle(fontWeight: FontWeight.w800)),
          Text(
            'Measured: ${measured?.toStringAsFixed(1) ?? 'Unavailable'} dB SPL',
          ),
          Text(
            'Current offset: ${currentOffset?.toStringAsFixed(1) ?? 'Unavailable'}',
          ),
          Text(
            'Suggested offset: ${suggested?.toStringAsFixed(1) ?? 'Unavailable'}',
          ),
          const Text('Apply status: Backend setter unavailable'),
        ],
      ),
    );
  }
}

class _ToolSection extends StatelessWidget {
  const _ToolSection({
    required this.title,
    required this.icon,
    required this.child,
  });

  final String title;
  final IconData icon;
  final Widget child;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(22),
    decoration: BoxDecoration(
      color: MainScreen.panel,
      borderRadius: BorderRadius.circular(24),
      border: Border.all(color: MainScreen.accentLight.withValues(alpha: 0.24)),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(icon, color: MainScreen.accentLight),
            const SizedBox(width: 10),
            Expanded(
              child: Text(
                title,
                style: Theme.of(
                  context,
                ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800),
              ),
            ),
          ],
        ),
        const SizedBox(height: 16),
        child,
      ],
    ),
  );
}

class _SignalCard extends StatelessWidget {
  const _SignalCard({
    required this.signal,
    required this.title,
    required this.description,
    required this.runLabel,
    required this.activeSignal,
    required this.isPreview,
    required this.isBlocked,
    required this.isStarting,
    required this.onPreview,
    required this.onRun,
    required this.onStop,
  });

  final CalibrationSignal signal;
  final String title;
  final String description;
  final String runLabel;
  final CalibrationSignal activeSignal;
  final bool isPreview;
  final bool isBlocked;
  final bool isStarting;
  final VoidCallback onPreview;
  final VoidCallback onRun;
  final VoidCallback onStop;

  @override
  Widget build(BuildContext context) {
    final isActive = isPreview && activeSignal == signal;
    return Semantics(
      label: title,
      value: isActive ? 'Preview playing' : 'Idle',
      child: Container(
        padding: const EdgeInsets.all(18),
        decoration: BoxDecoration(
          color: MainScreen.panelLight,
          borderRadius: BorderRadius.circular(18),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              title,
              style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 17),
            ),
            const SizedBox(height: 6),
            Text(
              description,
              style: const TextStyle(color: MainScreen.textMuted),
            ),
            const SizedBox(height: 4),
            const Text('Duration: 60 seconds'),
            const SizedBox(height: 6),
            const Text(
              'Starts the 60-second signal and synchronized four-node capture.',
              style: TextStyle(color: MainScreen.textMuted),
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                FilledButton.icon(
                  key: Key('run-${signal.name}'),
                  onPressed: isBlocked || isStarting ? null : onRun,
                  icon: const Icon(Icons.fiber_manual_record),
                  label: Text(runLabel),
                ),
                OutlinedButton.icon(
                  key: Key('play-${signal.name}'),
                  onPressed: isBlocked || isStarting ? null : onPreview,
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('Preview'),
                ),
                if (isActive)
                  OutlinedButton.icon(
                    key: Key('stop-${signal.name}'),
                    onPressed: onStop,
                    icon: const Icon(Icons.stop),
                    label: const Text('Stop'),
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _PlaybackProgress extends StatelessWidget {
  const _PlaybackProgress({
    required this.signal,
    required this.position,
    required this.duration,
    required this.remaining,
    required this.progress,
    required this.onStop,
  });

  final CalibrationSignal signal;
  final Duration position;
  final Duration duration;
  final Duration remaining;
  final double progress;
  final VoidCallback onStop;

  @override
  Widget build(BuildContext context) {
    final remainingSeconds = (remaining.inMilliseconds / 1000).ceil();
    return Semantics(
      label: 'Calibration signal preview progress',
      value:
          '${_format(position)} of ${_format(duration)}, $remainingSeconds seconds remaining',
      child: Container(
        padding: const EdgeInsets.all(18),
        decoration: BoxDecoration(
          color: MainScreen.accent.withValues(alpha: 0.12),
          borderRadius: BorderRadius.circular(18),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              'Previewing: ${_signalName(signal)}',
              style: const TextStyle(fontWeight: FontWeight.w800),
            ),
            const SizedBox(height: 10),
            Text('${_format(position)} / ${_format(duration)}'),
            Text('$remainingSeconds seconds remaining'),
            const SizedBox(height: 10),
            LinearProgressIndicator(value: progress),
            const SizedBox(height: 14),
            Align(
              alignment: Alignment.centerLeft,
              child: FilledButton.icon(
                key: const Key('stop-active-signal'),
                onPressed: onStop,
                icon: const Icon(Icons.stop),
                label: const Text('Stop'),
              ),
            ),
          ],
        ),
      ),
    );
  }

  static String _format(Duration duration) {
    final minutes = duration.inMinutes.remainder(60).toString().padLeft(2, '0');
    final seconds = duration.inSeconds.remainder(60).toString().padLeft(2, '0');
    return '$minutes:$seconds';
  }
}
