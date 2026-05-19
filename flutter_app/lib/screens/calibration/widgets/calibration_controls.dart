import 'package:flutter/material.dart';

class CalibrationControls extends StatelessWidget {
  const CalibrationControls({
    super.key,
    required this.onStart,
    required this.onStop,
    required this.isRunning,
  });

  final VoidCallback onStart;
  final VoidCallback onStop;
  final bool isRunning;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: FilledButton.icon(
            onPressed: isRunning ? null : onStart,
            icon: const Icon(Icons.play_arrow),
            label: const Text('Start Calibration'),
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: OutlinedButton.icon(
            onPressed: isRunning ? onStop : null,
            icon: const Icon(Icons.stop),
            label: const Text('Stop Calibration'),
          ),
        ),
      ],
    );
  }
}
