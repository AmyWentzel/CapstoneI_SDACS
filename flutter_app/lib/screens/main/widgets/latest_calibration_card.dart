import 'package:flutter/material.dart';

import '../../../models/calibration_result.dart';
import '../../../widgets/sdacs_status_badge.dart';

class LatestCalibrationCard extends StatelessWidget {
  const LatestCalibrationCard({super.key, required this.result});

  final CalibrationResult result;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;

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
                    'Latest Calibration',
                    style: textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
                SdacsStatusBadge(status: result.status),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'Reference: ${result.referenceSplDb.toStringAsFixed(1)} dB SPL',
            ),
            Text('Measured: ${result.measuredSplDb.toStringAsFixed(1)} dB SPL'),
            Text('Offset: ${result.calibrationOffsetDb.toStringAsFixed(1)} dB'),
          ],
        ),
      ),
    );
  }
}
