import 'package:flutter/material.dart';

import '../../../models/calibration_result.dart';
import '../../../widgets/sdacs_status_badge.dart';

class CalibrationStatusPanel extends StatelessWidget {
  const CalibrationStatusPanel({
    super.key,
    required this.status,
    this.latestResult,
  });

  final String status;
  final CalibrationResult? latestResult;

  @override
  Widget build(BuildContext context) {
    final result = latestResult;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    'Calibration Status',
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ),
                SdacsStatusBadge(status: status),
              ],
            ),
            const SizedBox(height: 12),
            if (result == null)
              const Text('No calibration has been run in this session.')
            else ...[
              Text(
                'Reference: ${result.referenceFrequencyHz.toStringAsFixed(0)} Hz',
              ),
              Text(
                'Reference level: ${result.referenceSplDb.toStringAsFixed(1)} dB',
              ),
              Text(
                'Measured level: ${result.measuredSplDb.toStringAsFixed(1)} dB',
              ),
              Text(
                'Offset: ${result.calibrationOffsetDb.toStringAsFixed(1)} dB',
              ),
            ],
          ],
        ),
      ),
    );
  }
}
