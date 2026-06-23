import 'package:flutter/material.dart';

import '../../models/calibration_result.dart';
import '../../services/calibration_service.dart';
import '../../services/sdacs_api_service.dart';
import '../../widgets/sdacs_app_bar.dart';
import 'widgets/calibration_controls.dart';
import 'widgets/calibration_delay_input.dart';
import 'widgets/calibration_status_panel.dart';

class CalibrationScreen extends StatefulWidget {
  const CalibrationScreen({super.key});

  @override
  State<CalibrationScreen> createState() => _CalibrationScreenState();
}

class _CalibrationScreenState extends State<CalibrationScreen> {
  final CalibrationService _service = const CalibrationService();
  final SdacsApiService _apiService = const SdacsApiService();
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
    final delayMs = int.tryParse(_delayController.text) ?? 0;
    final durationSeconds = int.tryParse(_durationController.text) ?? 30;

    setState(() {
      _isRunning = true;
      _status = 'running';
    });

    try {
      final session = await _apiService.startCapture(
        delayMs: delayMs,
        recordSeconds: durationSeconds,
      );

      if (!mounted) {
        return;
      }

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Capture command sent: ${session.sessionId}')),
      );
    } on SdacsApiException catch (error) {
      if (!mounted) {
        return;
      }

      setState(() {
        _isRunning = false;
        _status = 'backend offline';
      });

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Capture failed: ${error.message}')),
      );
    }
  }

  Future<void> _stopCalibration() async {
    await _service.stopCalibration();
    final latestResult = await _service.getLatestCalibration();

    if (!mounted) {
      return;
    }

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
    return Scaffold(
      appBar: const SdacsAppBar(title: 'Calibration'),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          CalibrationDelayInput(
            delayController: _delayController,
            durationController: _durationController,
          ),
          const SizedBox(height: 16),
          CalibrationControls(
            isRunning: _isRunning,
            onStart: _startCalibration,
            onStop: _stopCalibration,
          ),
          const SizedBox(height: 16),
          CalibrationStatusPanel(status: _status, latestResult: _latestResult),
        ],
      ),
    );
  }
}
