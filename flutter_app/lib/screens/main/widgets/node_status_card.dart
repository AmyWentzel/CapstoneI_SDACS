import 'package:flutter/material.dart';

import '../../../models/node_telemetry.dart';
import '../../../widgets/sdacs_status_badge.dart';

class NodeStatusCard extends StatelessWidget {
  const NodeStatusCard({super.key, required this.telemetry});

  final NodeTelemetry telemetry;

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
                    telemetry.nodeId,
                    style: textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
                SdacsStatusBadge(status: telemetry.status),
              ],
            ),
            const SizedBox(height: 12),
            Text('${telemetry.dbSpl.toStringAsFixed(1)} dB SPL'),
            Text('RMS ${telemetry.rms.toStringAsFixed(4)}'),
            Text('${telemetry.dbfs.toStringAsFixed(1)} dBFS'),
            Text('${telemetry.peakFrequencyHz.toStringAsFixed(0)} Hz peak'),
            Text('${telemetry.temperatureC.toStringAsFixed(1)} C'),
            Text('${telemetry.humidityPercent.toStringAsFixed(1)}% RH'),
            Text('${telemetry.batterySoc.toStringAsFixed(0)}% battery'),
            Text('FW ${telemetry.firmwareVersion}'),
          ],
        ),
      ),
    );
  }
}
