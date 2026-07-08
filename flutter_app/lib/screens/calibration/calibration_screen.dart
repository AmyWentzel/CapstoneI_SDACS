import 'package:flutter/material.dart';

import '../../models/calibration_result.dart';
import '../../services/calibration_service.dart';
import 'widgets/calibration_controls.dart';
import 'widgets/calibration_delay_input.dart';
import 'widgets/calibration_status_panel.dart';

class CalibrationScreen extends StatefulWidget {
  const CalibrationScreen({super.key});

  @override
  State<CalibrationScreen> createState() => _CalibrationScreenState();
}

class _CalibrationScreenState extends State<CalibrationScreen> {
  static const background = Color(0xFF08080C);
  static const panel = Color(0xFF12121A);
  static const panelLight = Color(0xFF1A1A25);
  static const accent = Color(0xFF8B5CF6);
  static const accentLight = Color(0xFFA78BFA);
  static const textMuted = Color(0xFFA1A1AA);

  final CalibrationService _service = const CalibrationService();
  final _delayController = TextEditingController(text: '0');
  final _durationController = TextEditingController(text: '30');

  bool _isRunning = false;
  String _status = 'ready';
  CalibrationResult? _latestResult;

  @override
  void dispose() {
    _delayController.dispose();
    _durationController.dispose();
    super.dispose();
  }

  Future<void> _startCalibration() async {
    setState(() {
      _isRunning = true;
      _status = 'running';
    });

    await _service.startCalibration(
      delayMs: int.tryParse(_delayController.text) ?? 0,
      durationSeconds: int.tryParse(_durationController.text) ?? 30,
    );

    if (!mounted) return;

    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Placeholder calibration started.')),
    );
  }

  Future<void> _stopCalibration() async {
    await _service.stopCalibration();
    final latestResult = await _service.getLatestCalibration();

    if (!mounted) return;

    setState(() {
      _isRunning = false;
      _status = 'ready';
      _latestResult = latestResult;
    });

    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Placeholder calibration stopped.')),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Theme(
      data: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: background,
        cardColor: panel,
        colorScheme: const ColorScheme.dark(
          primary: accent,
          secondary: accentLight,
          surface: panel,
        ),
        inputDecorationTheme: InputDecorationTheme(
          filled: true,
          fillColor: panelLight,
          labelStyle: const TextStyle(color: textMuted),
          hintStyle: const TextStyle(color: textMuted),
          enabledBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
            borderSide: BorderSide(color: accent.withOpacity(0.25)),
          ),
          focusedBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
            borderSide: const BorderSide(color: accentLight, width: 1.4),
          ),
          border: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
          ),
        ),
      ),
      child: Scaffold(
        appBar: AppBar(
          title: const Text(
            'CALIBRATION',
            style: TextStyle(
              fontWeight: FontWeight.w900,
              letterSpacing: 1.6,
              fontSize: 15,
            ),
          ),
          centerTitle: true,
          backgroundColor: background,
          foregroundColor: Colors.white,
          elevation: 0,
        ),
        body: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 10, 24, 32),
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 1200),
              child: Column(
                children: [
                  _CalibrationHero(isRunning: _isRunning),
                  const SizedBox(height: 28),
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final isWide = constraints.maxWidth >= 950;

                      if (isWide) {
                        return Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(
                              flex: 4,
                              child: _CalibrationCard(
                                icon: Icons.timer_outlined,
                                title: 'Capture Settings',
                                subtitle:
                                    'Set the delay and capture duration before starting calibration.',
                                child: CalibrationDelayInput(
                                  delayController: _delayController,
                                  durationController: _durationController,
                                ),
                              ),
                            ),
                            const SizedBox(width: 22),
                            Expanded(
                              flex: 3,
                              child: _CalibrationCard(
                                icon: Icons.play_circle_outline,
                                title: 'Calibration Controls',
                                subtitle:
                                    'Start or stop the SPL calibration sequence.',
                                child: CalibrationControls(
                                  isRunning: _isRunning,
                                  onStart: _startCalibration,
                                  onStop: _stopCalibration,
                                ),
                              ),
                            ),
                          ],
                        );
                      }

                      return Column(
                        children: [
                          _CalibrationCard(
                            icon: Icons.timer_outlined,
                            title: 'Capture Settings',
                            subtitle:
                                'Set the delay and capture duration before starting calibration.',
                            child: CalibrationDelayInput(
                              delayController: _delayController,
                              durationController: _durationController,
                            ),
                          ),
                          const SizedBox(height: 22),
                          _CalibrationCard(
                            icon: Icons.play_circle_outline,
                            title: 'Calibration Controls',
                            subtitle: 'Start or stop the SPL calibration sequence.',
                            child: CalibrationControls(
                              isRunning: _isRunning,
                              onStart: _startCalibration,
                              onStop: _stopCalibration,
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 22),
                  _CalibrationCard(
                    icon: Icons.monitor_heart_outlined,
                    title: 'Calibration Status',
                    subtitle:
                        'View the current calibration state and latest result returned by the system.',
                    child: CalibrationStatusPanel(
                      status: _status,
                      latestResult: _latestResult,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _CalibrationHero extends StatelessWidget {
  const _CalibrationHero({required this.isRunning});

  final bool isRunning;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 40),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(34),
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Color(0xFF181020),
            Color(0xFF101018),
            Color(0xFF08080C),
          ],
        ),
      ),
      child: Column(
        children: [
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: _CalibrationScreenState.accent.withOpacity(0.14),
              shape: BoxShape.circle,
            ),
            child: Icon(
              isRunning ? Icons.graphic_eq : Icons.tune_rounded,
              color: _CalibrationScreenState.accentLight,
              size: 42,
            ),
          ),
          const SizedBox(height: 22),
          Text(
            isRunning ? 'Calibration Running' : 'SPL Calibration',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.displaySmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w900,
                ),
          ),
          const SizedBox(height: 12),
          const Text(
            'Run a controlled calibration sequence to compare the reference SPL against measured node readings.',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: _CalibrationScreenState.textMuted,
              fontSize: 15,
              height: 1.5,
            ),
          ),
          const SizedBox(height: 22),
          _StatusPill(isRunning: isRunning),
        ],
      ),
    );
  }
}

class _StatusPill extends StatelessWidget {
  const _StatusPill({required this.isRunning});

  final bool isRunning;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 9),
      decoration: BoxDecoration(
        color: isRunning
            ? _CalibrationScreenState.accent.withOpacity(0.18)
            : _CalibrationScreenState.panelLight,
        borderRadius: BorderRadius.circular(999),
        border: Border.all(
          color: isRunning
              ? _CalibrationScreenState.accentLight.withOpacity(0.5)
              : _CalibrationScreenState.accent.withOpacity(0.25),
        ),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            isRunning ? Icons.circle : Icons.check_circle_outline,
            size: 13,
            color: isRunning
                ? _CalibrationScreenState.accentLight
                : Colors.greenAccent,
          ),
          const SizedBox(width: 8),
          Text(
            isRunning ? 'RUNNING' : 'READY',
            style: const TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.w900,
              letterSpacing: 1.1,
              fontSize: 12,
            ),
          ),
        ],
      ),
    );
  }
}

class _CalibrationCard extends StatelessWidget {
  const _CalibrationCard({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.child,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: _CalibrationScreenState.panel,
        borderRadius: BorderRadius.circular(30),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                padding: const EdgeInsets.all(11),
                decoration: BoxDecoration(
                  color: _CalibrationScreenState.accent.withOpacity(0.12),
                  borderRadius: BorderRadius.circular(16),
                ),
                child: Icon(
                  icon,
                  color: _CalibrationScreenState.accentLight,
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Text(
                  title,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 20,
                    fontWeight: FontWeight.w900,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Text(
            subtitle,
            style: const TextStyle(
              color: _CalibrationScreenState.textMuted,
              height: 1.4,
            ),
          ),
          const SizedBox(height: 22),
          child,
        ],
      ),
    );
  }
}