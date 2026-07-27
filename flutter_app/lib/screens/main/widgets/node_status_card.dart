import 'package:flutter/material.dart';

import '../../../models/capture_node_metric.dart';
import '../../../models/node_telemetry.dart';
import '../../../widgets/sdacs_status_badge.dart';

class NodeTelemetryFormat {
  const NodeTelemetryFormat._();

  static String number(double? value, int decimals, {String suffix = ''}) =>
      value == null || !value.isFinite
      ? '—'
      : '${value.toStringAsFixed(decimals)}$suffix';

  static String rms(double? value) =>
      value == null || !value.isFinite ? '—' : value.toStringAsFixed(6);

  static String peak(double? value) {
    if (value == null || !value.isFinite) return 'No data';
    return value >= 1000
        ? '${(value / 1000).toStringAsFixed(2)} kHz'
        : '${value.toStringAsFixed(1)} Hz';
  }

  static String firmware(String? value) =>
      value == null || value.trim().isEmpty ? 'Unknown' : value;
}

class NodeStatusCard extends StatefulWidget {
  const NodeStatusCard({
    super.key,
    required this.telemetry,
    this.captureMetric,
    this.latestCaptureId,
    this.capturedAt,
  });

  final NodeTelemetry telemetry;
  final CaptureNodeMetric? captureMetric;
  final String? latestCaptureId;
  final DateTime? capturedAt;

  @override
  State<NodeStatusCard> createState() => _NodeStatusCardState();
}

class _NodeStatusCardState extends State<NodeStatusCard> {
  bool _detailsExpanded = false;

  @override
  Widget build(BuildContext context) {
    final telemetry = widget.telemetry;
    final capture = widget.captureMetric;
    final textTheme = Theme.of(context).textTheme;
    return Card(
      child: LayoutBuilder(
        builder: (context, constraints) => Padding(
          padding: const EdgeInsets.all(8),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(
                      telemetry.nodeId,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  SdacsStatusBadge(status: telemetry.status),
                ],
              ),
              const SizedBox(height: 4),
              Text(
                capture == null ? 'No capture data' : 'Latest Test',
                style: textTheme.labelMedium,
              ),
              Wrap(
                spacing: 8,
                runSpacing: 6,
                children: [
                  _PrimaryMetric(
                    label: 'Estimated SPL',
                    value: NodeTelemetryFormat.number(
                      capture?.estimatedSplDb,
                      1,
                      suffix: ' dB SPL',
                    ),
                  ),
                  _PrimaryMetric(
                    label: 'Peak',
                    value: NodeTelemetryFormat.peak(capture?.peakFrequencyHz),
                  ),
                ],
              ),
              const SizedBox(height: 2),
              InkWell(
                onTap: () =>
                    setState(() => _detailsExpanded = !_detailsExpanded),
                child: Padding(
                  padding: EdgeInsets.zero,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(_detailsExpanded ? 'Hide details' : 'Details'),
                      Icon(
                        _detailsExpanded
                            ? Icons.expand_less
                            : Icons.expand_more,
                        size: 18,
                      ),
                    ],
                  ),
                ),
              ),
              if (_detailsExpanded)
                Expanded(
                  child: SingleChildScrollView(
                    primary: false,
                    child: Wrap(
                      spacing: 14,
                      runSpacing: 6,
                      children: [
                        _DetailMetric(
                          label: 'RMS',
                          value: NodeTelemetryFormat.rms(capture?.rms),
                        ),
                        _DetailMetric(
                          label: 'dBFS',
                          value: NodeTelemetryFormat.number(
                            capture?.dbfs,
                            1,
                            suffix: ' dBFS',
                          ),
                        ),
                        _DetailMetric(
                          label: 'Temperature',
                          value: NodeTelemetryFormat.number(
                            telemetry.temperatureC,
                            1,
                            suffix: ' °C',
                          ),
                        ),
                        _DetailMetric(
                          label: 'Humidity',
                          value: NodeTelemetryFormat.number(
                            telemetry.humidityPercent,
                            1,
                            suffix: '% RH',
                          ),
                        ),
                        _DetailMetric(
                          label: 'Battery',
                          value: NodeTelemetryFormat.number(
                            telemetry.batterySoc,
                            0,
                            suffix: '%',
                          ),
                        ),
                        _DetailMetric(
                          label: 'BLE RSSI',
                          value: telemetry.bleRssiDbm == null
                              ? 'No reading'
                              : '${telemetry.bleRssiDbm} dBm',
                        ),
                        SizedBox(
                          width: constraints.maxWidth,
                          child: Text(
                            'Firmware: ${NodeTelemetryFormat.firmware(telemetry.firmwareVersion)}',
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PrimaryMetric extends StatelessWidget {
  const _PrimaryMetric({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => ConstrainedBox(
    constraints: const BoxConstraints(minWidth: 82),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(label, style: Theme.of(context).textTheme.labelSmall),
        Text(
          value,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(fontWeight: FontWeight.w600),
        ),
      ],
    ),
  );
}

class _DetailMetric extends StatelessWidget {
  const _DetailMetric({required this.label, required this.value});
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: 118,
    child: Text('$label: $value', maxLines: 1, overflow: TextOverflow.ellipsis),
  );
}
