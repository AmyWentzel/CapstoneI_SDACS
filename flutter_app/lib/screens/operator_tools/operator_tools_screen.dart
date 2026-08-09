// SDACS Flutter component: Flutter UI implementation for the operator tools screen portion of the SDACS operator workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../models/capture_session.dart';
import '../../models/spl_calibration.dart';
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
    this.apiService,
  });

  final CalibrationAudio? audioService;
  final OperationController? operationController;
  final CalibrationCaptureCoordinator? coordinator;
  final SdacsApiService? apiService;

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
  SdacsApiService? _api;
  StreamSubscription<Duration>? _positionSubscription;
  StreamSubscription<Duration?>? _durationSubscription;
  StreamSubscription<CalibrationPlaybackState>? _stateSubscription;
  final _referenceSplController = TextEditingController();
  Duration _previewPosition = Duration.zero;
  Duration _previewDuration = signalDuration;
  bool _starting = false;
  bool _calculatingOffsets = false;
  bool _applyingOffsets = false;
  bool _verificationPending = false;
  bool _lastPreviewWasVerification = false;
  String? _referenceError;
  String? _lastPreviewedCaptureId;
  SplCalibrationPreview? _calibrationPreview;
  SplCalibrationApplyResult? _applyResult;

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
    if (!mounted) return;
    final result = _latestOneKhzResult;
    setState(() {
      if (result != null &&
          _calibrationPreview != null &&
          _calibrationPreview!.captureId != result.captureId) {
        _calibrationPreview = null;
        _applyResult = null;
      }
    });
    final reference = _parseReference(showError: false);
    if (_operations.operation == ActiveOperation.idle &&
        result != null &&
        result.captureId != _lastPreviewedCaptureId &&
        reference != null &&
        !_calculatingOffsets) {
      unawaited(_calculateOffsets());
    }
  }

  CaptureCombinedResult? get _latestOneKhzResult =>
      _operations.latestCalibrationSignal == CalibrationSignal.oneKhzTone
      ? _operations.latestCalibrationResult
      : null;

  SdacsApiService _apiService() => _api ??=
      widget.apiService ??
      SdacsApiService(config: BackendConfigScope.configOf(context));

  CalibrationCaptureCoordinator _captureCoordinator() {
    return _coordinator ??= CalibrationCaptureCoordinator(
      api: _apiService(),
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

  double? _parseReference({bool showError = true}) {
    final reference = double.tryParse(_referenceSplController.text.trim());
    final valid = reference != null &&
        reference.isFinite &&
        reference >= minimumReferenceSpl &&
        reference <= maximumReferenceSpl;
    if (!valid && showError && mounted) {
      setState(() {
        _referenceError =
            'Enter a finite reference SPL from $minimumReferenceSpl to $maximumReferenceSpl dB.';
      });
    }
    return valid ? reference : null;
  }

  Future<void> _calculateOffsets() async {
    if (_calculatingOffsets || _applyingOffsets) return;
    final reference = _parseReference();
    final captureId = _latestOneKhzResult?.captureId;
    if (reference == null || captureId == null) return;
    final verificationResult = _verificationPending;
    setState(() {
      _referenceError = null;
      _calculatingOffsets = true;
      _applyResult = null;
    });
    try {
      final preview = await _apiService().previewSplCalibration(
        captureId: captureId,
        referenceSplDb: reference,
      );
      if (!mounted) return;
      setState(() {
        _calibrationPreview = preview;
        _lastPreviewedCaptureId = captureId;
        _lastPreviewWasVerification = verificationResult;
        _verificationPending = false;
      });
    } on SdacsApiException catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Unable to calculate offsets: ${error.message}')),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Unable to calculate offsets: $error')),
      );
    } finally {
      if (mounted) setState(() => _calculatingOffsets = false);
    }
  }

  Future<void> _applyOffsets() async {
    final preview = _calibrationPreview;
    if (_applyingOffsets || _calculatingOffsets || preview == null || !preview.canApply) {
      return;
    }
    final offsets = <String, double>{
      for (final node in preview.nodes)
        if (node.suggestedOffsetDb != null) node.nodeId: node.suggestedOffsetDb!,
    };
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Apply SPL calibration offsets?'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Each node will save the new additive dB SPL offset in persistent firmware storage.',
            ),
            const SizedBox(height: 12),
            ...preview.nodes.map(
              (node) => Text(
                '${node.nodeId}: ${node.suggestedOffsetDb?.toStringAsFixed(2)} dB',
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Apply to Four Nodes'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;

    setState(() {
      _applyingOffsets = true;
      _applyResult = null;
    });
    try {
      final result = await _apiService().applySplCalibration(
        captureId: preview.captureId,
        referenceSplDb: preview.referenceSplDb,
        nodeOffsetsDb: offsets,
      );
      if (!mounted) return;
      setState(() => _applyResult = result);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            result.complete
                ? 'Calibration offsets were acknowledged and saved by all four nodes.'
                : 'Calibration apply completed with status: ${result.status}.',
          ),
        ),
      );
    } on SdacsApiException catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Unable to apply offsets: ${error.message}')),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Unable to apply offsets: $error')),
      );
    } finally {
      if (mounted) setState(() => _applyingOffsets = false);
    }
  }

  Future<void> _runVerification() async {
    if (_starting || _isBlocking || _applyResult?.complete != true) return;
    setState(() {
      _starting = true;
      _verificationPending = true;
    });
    try {
      final session = await _captureCoordinator().start(
        CalibrationSignal.oneKhzTone,
        verification: true,
      );
      if (session != null && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              'Verification capture started: ${session.captureId}. The result will be checked against ±${_calibrationPreview?.toleranceDb.toStringAsFixed(1) ?? '1.0'} dB.',
            ),
          ),
        );
      }
    } catch (error) {
      if (mounted) {
        setState(() => _verificationPending = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('$error')),
        );
      }
    } finally {
      if (mounted) setState(() => _starting = false);
    }
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
                                          'Co-locate all four node microphones beside a reference sound-level meter during this capture.',
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
                          captureId: _latestOneKhzResult?.captureId,
                          preview: _calibrationPreview,
                          applyResult: _applyResult,
                          referenceController: _referenceSplController,
                          referenceError: _referenceError,
                          calculating: _calculatingOffsets,
                          applying: _applyingOffsets,
                          verificationPending: _verificationPending,
                          verificationResult: _lastPreviewWasVerification,
                          isBlocked: _isBlocking,
                          onCalculate: _calculateOffsets,
                          onApply: _applyOffsets,
                          onVerify: _runVerification,
                        ),
                        const SizedBox(height: 18),
                        _ToolSection(
                          title: 'Manual Diagnostics',
                          icon: Icons.tune,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text(
                                'The SPL workflow above is backend-authoritative. Open the existing controls only for additional manual sensor diagnostics.',
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
                                  'Open Manual Calibration Controls',
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

class _SplCalibrationSection extends StatelessWidget {
  const _SplCalibrationSection({
    required this.captureId,
    required this.preview,
    required this.applyResult,
    required this.referenceController,
    required this.referenceError,
    required this.calculating,
    required this.applying,
    required this.verificationPending,
    required this.verificationResult,
    required this.isBlocked,
    required this.onCalculate,
    required this.onApply,
    required this.onVerify,
  });

  final String? captureId;
  final SplCalibrationPreview? preview;
  final SplCalibrationApplyResult? applyResult;
  final TextEditingController referenceController;
  final String? referenceError;
  final bool calculating;
  final bool applying;
  final bool verificationPending;
  final bool verificationResult;
  final bool isBlocked;
  final VoidCallback onCalculate;
  final VoidCallback onApply;
  final VoidCallback onVerify;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return _ToolSection(
      title: 'SPL Calibration',
      icon: Icons.speed_outlined,
      child: captureId == null
          ? const Text(
              'Run a 1 kHz Calibration Capture to calculate suggested node offsets.',
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text(
                  '1 kHz capture: $captureId',
                  style: Theme.of(context).textTheme.titleSmall?.copyWith(
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const SizedBox(height: 6),
                const Text(
                  'Enter the reference meter reading measured beside the clustered node microphones. The backend uses the capture median dBFS and validates the dedicated 1 kHz detector before proposing firmware offsets.',
                ),
                const SizedBox(height: 14),
                TextField(
                  key: const Key('reference-spl-input'),
                  controller: referenceController,
                  enabled: !calculating && !applying && !isBlocked,
                  keyboardType: const TextInputType.numberWithOptions(
                    decimal: true,
                  ),
                  decoration: InputDecoration(
                    labelText: 'Reference sound-level meter reading (dB SPL)',
                    helperText: 'Accepted range: 30–140 dB SPL',
                    errorText: referenceError,
                  ),
                ),
                const SizedBox(height: 12),
                OutlinedButton.icon(
                  key: const Key('calculate-offsets'),
                  onPressed: calculating || applying || isBlocked
                      ? null
                      : onCalculate,
                  icon: calculating
                      ? const SizedBox.square(
                          dimension: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.calculate_outlined),
                  label: Text(
                    calculating
                        ? 'Calculating…'
                        : preview == null
                        ? 'Calculate Suggested Offsets'
                        : 'Refresh Suggested Offsets',
                  ),
                ),
                if (preview != null) ...[
                  const SizedBox(height: 16),
                  _CalibrationStatusBanner(
                    preview: preview!,
                    verificationPending: verificationPending,
                    verificationResult: verificationResult,
                  ),
                  const SizedBox(height: 12),
                  ...preview!.nodes.map(
                    (node) => Padding(
                      padding: const EdgeInsets.only(bottom: 10),
                      child: _CalibrationNodeCard(
                        node: node,
                        applyResult: _applyForNode(applyResult, node.nodeId),
                      ),
                    ),
                  ),
                  if (preview!.warnings.isNotEmpty) ...[
                    const SizedBox(height: 2),
                    ...preview!.warnings.map(
                      (warning) => Text(
                        '• $warning',
                        style: TextStyle(color: colors.tertiary),
                      ),
                    ),
                  ],
                  const SizedBox(height: 6),
                  FilledButton.icon(
                    key: const Key('apply-offsets'),
                    onPressed:
                        preview!.canApply && !applying && !calculating && !isBlocked
                        ? onApply
                        : null,
                    icon: applying
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.save_outlined),
                    label: Text(
                      applying ? 'Applying to Nodes…' : 'Apply Confirmed Offsets',
                    ),
                  ),
                  if (!preview!.canApply) ...[
                    const SizedBox(height: 8),
                    Text(
                      'Offsets remain locked until all four nodes pass sample count, 1 kHz tone, clipping, and firmware-range checks.',
                      style: TextStyle(color: colors.tertiary),
                    ),
                  ],
                  if (applyResult != null) ...[
                    const SizedBox(height: 14),
                    _CalibrationApplyBanner(result: applyResult!),
                  ],
                  if (applyResult?.complete == true) ...[
                    const SizedBox(height: 10),
                    OutlinedButton.icon(
                      key: const Key('run-calibration-verification'),
                      onPressed: isBlocked ? null : onVerify,
                      icon: const Icon(Icons.verified_outlined),
                      label: const Text('Run 1 kHz Verification Capture'),
                    ),
                  ],
                ],
              ],
            ),
    );
  }

  static SplCalibrationNodeApplyResult? _applyForNode(
    SplCalibrationApplyResult? result,
    String nodeId,
  ) {
    if (result == null) return null;
    for (final row in result.nodes) {
      if (row.nodeId == nodeId) return row;
    }
    return null;
  }
}

class _CalibrationStatusBanner extends StatelessWidget {
  const _CalibrationStatusBanner({
    required this.preview,
    required this.verificationPending,
    required this.verificationResult,
  });

  final SplCalibrationPreview preview;
  final bool verificationPending;
  final bool verificationResult;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final verificationPassed = verificationResult && preview.allWithinTolerance;
    final verificationFailed =
        verificationResult && preview.status == 'ready' && !preview.allWithinTolerance;
    final icon = verificationPassed
        ? Icons.verified
        : verificationFailed
        ? Icons.error_outline
        : preview.status == 'ready'
        ? Icons.check_circle_outline
        : preview.status == 'partial'
        ? Icons.warning_amber_rounded
        : Icons.error_outline;
    final color = verificationPassed
        ? Colors.greenAccent
        : verificationFailed
        ? colors.error
        : preview.status == 'ready'
        ? colors.primary
        : preview.status == 'partial'
        ? colors.tertiary
        : colors.error;
    final text = verificationPending
        ? 'Verification capture is processing.'
        : verificationPassed
        ? 'Verification passed: all four nodes are within ±${preview.toleranceDb.toStringAsFixed(1)} dB.'
        : verificationFailed
        ? 'Verification failed: at least one node is outside ±${preview.toleranceDb.toStringAsFixed(1)} dB.'
        : preview.status == 'ready'
        ? 'Ready to apply: all four nodes passed calibration data checks.'
        : preview.status == 'partial'
        ? 'Partial result: correct the flagged node data and repeat the capture.'
        : 'Invalid calibration capture: offsets cannot be applied.';
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.10),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.45)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: color),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
          ),
        ],
      ),
    );
  }
}

class _CalibrationNodeCard extends StatelessWidget {
  const _CalibrationNodeCard({required this.node, required this.applyResult});

  final SplCalibrationNodePreview node;
  final SplCalibrationNodeApplyResult? applyResult;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final stateColor = node.eligible ? colors.primary : colors.error;
    final tonePercent = node.toneDetectionRate == null
        ? 'Unavailable'
        : '${(node.toneDetectionRate! * 100).toStringAsFixed(0)}% '
              '(${node.toneDetectedCount}/${node.toneSampleCount})';
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: MainScreen.background.withValues(alpha: 0.34),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: stateColor.withValues(alpha: 0.30)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  node.nodeId,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
              ),
              Icon(
                node.eligible ? Icons.check_circle : Icons.cancel_outlined,
                color: stateColor,
                size: 19,
              ),
              const SizedBox(width: 5),
              Text(node.eligible ? 'Eligible' : 'Blocked'),
            ],
          ),
          const SizedBox(height: 9),
          Wrap(
            spacing: 18,
            runSpacing: 8,
            children: [
              _CalibrationMetric(
                label: 'Samples',
                value: '${node.sampleCount}',
              ),
              _CalibrationMetric(
                label: 'Measured dBFS',
                value: _db(node.measuredDbfs, 'dBFS'),
              ),
              _CalibrationMetric(
                label: 'Measured SPL',
                value: _db(node.measuredSplDb, 'dB SPL'),
              ),
              _CalibrationMetric(
                label: 'Current offset',
                value: _db(node.currentOffsetDb, 'dB'),
              ),
              _CalibrationMetric(
                label: 'Suggested offset',
                value: _db(node.suggestedOffsetDb, 'dB'),
              ),
              _CalibrationMetric(
                label: 'Adjustment',
                value: _signedDb(node.adjustmentDb),
              ),
              _CalibrationMetric(
                label: '1 kHz peak',
                value: node.representativeFrequencyHz == null
                    ? 'Unavailable'
                    : '${node.representativeFrequencyHz!.toStringAsFixed(1)} Hz',
              ),
              _CalibrationMetric(
                label: 'Tone detected',
                value: tonePercent,
              ),
              _CalibrationMetric(
                label: 'Meter error',
                value: _signedDb(node.measurementErrorDb),
              ),
              _CalibrationMetric(
                label: 'Tolerance',
                value: node.withinTolerance == null
                    ? 'Unavailable'
                    : node.withinTolerance!
                    ? 'Pass'
                    : 'Outside tolerance',
              ),
            ],
          ),
          if (node.warnings.isNotEmpty) ...[
            const SizedBox(height: 9),
            ...node.warnings.map(
              (warning) => Text(
                '• $warning',
                style: TextStyle(color: colors.tertiary),
              ),
            ),
          ],
          if (applyResult != null) ...[
            const Divider(height: 20),
            Text(
              applyResult!.applied
                  ? 'Apply status: saved and acknowledged · reported ${applyResult!.reportedOffsetDb?.toStringAsFixed(2) ?? '—'} dB'
                  : 'Apply status: ${_applyFailureLabel(applyResult!)}',
              style: TextStyle(
                color: applyResult!.applied ? Colors.greenAccent : colors.error,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ],
      ),
    );
  }

  static String _db(double? value, String suffix) => value == null
      ? 'Unavailable'
      : '${value.toStringAsFixed(2)} $suffix';

  static String _signedDb(double? value) => value == null
      ? 'Unavailable'
      : '${value >= 0 ? '+' : ''}${value.toStringAsFixed(2)} dB';

  static String _applyFailureLabel(SplCalibrationNodeApplyResult row) {
    if (!row.published) return 'MQTT publish failed';
    if (!row.acknowledged) return 'firmware acknowledgement timed out';
    return row.reason?.replaceAll('_', ' ') ?? 'firmware rejected the offset';
  }
}

class _CalibrationMetric extends StatelessWidget {
  const _CalibrationMetric({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: 155,
    child: Text.rich(
      TextSpan(
        children: [
          TextSpan(
            text: '$label: ',
            style: const TextStyle(fontWeight: FontWeight.w700),
          ),
          TextSpan(text: value),
        ],
      ),
    ),
  );
}

class _CalibrationApplyBanner extends StatelessWidget {
  const _CalibrationApplyBanner({required this.result});

  final SplCalibrationApplyResult result;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final color = result.complete ? Colors.greenAccent : colors.error;
    final applied = result.nodes.where((node) => node.applied).length;
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.10),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.45)),
      ),
      child: Text(
        result.complete
            ? 'Offsets saved by all four nodes. Run a new verification capture before treating SPL as calibrated.'
            : 'Apply result: $applied of ${result.nodes.length} nodes confirmed. Review each node status before retrying.',
        style: const TextStyle(fontWeight: FontWeight.w700),
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
