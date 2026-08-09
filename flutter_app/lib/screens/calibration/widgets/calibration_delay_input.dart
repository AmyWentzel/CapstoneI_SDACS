// SDACS Flutter component: Flutter UI implementation for the calibration delay input portion of the SDACS operator workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class CalibrationDelayInput extends StatelessWidget {
  const CalibrationDelayInput({
    super.key,
    required this.delayController,
    required this.durationController,
  });

  final TextEditingController delayController;
  final TextEditingController durationController;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: TextFormField(
            controller: delayController,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: 'Delay (ms)'),
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: TextFormField(
            controller: durationController,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: 'Duration (s)'),
          ),
        ),
      ],
    );
  }
}
