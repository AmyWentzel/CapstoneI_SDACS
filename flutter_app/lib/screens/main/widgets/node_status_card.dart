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

  static String compact(double? value, {String suffix = ''}) {
    if (value == null || !value.isFinite) return '\u2014';
    final formatted = value == value.roundToDouble()
        ? value.toStringAsFixed(0)
        : value.toStringAsFixed(2);
    return '$formatted$suffix';
  }

  static String gain(double? value) => compact(value, suffix: '\u00d7');
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
                capture == null ? 'No capture data' : 'Latest Capture',
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
                        SizedBox(
                          width: constraints.maxWidth,
                          child: _SceneProcessing(telemetry: telemetry),
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

class _SceneProcessing extends StatelessWidget {
  const _SceneProcessing({required this.telemetry});

  final NodeTelemetry telemetry;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final audioError = telemetry.audioError?.trim();
    final pollingWarning =
        audioError?.toLowerCase() == 'i2s_timeouts' &&
        telemetry.sampleRateOk == true;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Divider(height: 14),
        Text(
          'Scene Processing',
          style: Theme.of(
            context,
          ).textTheme.labelLarge?.copyWith(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 4),
        Wrap(
          spacing: 14,
          runSpacing: 6,
          children: [
            _DetailMetric(
              label: 'Scene level',
              value: NodeTelemetryFormat.number(
                telemetry.sceneDbfs,
                2,
                suffix: ' dBFS',
              ),
            ),
            _DetailMetric(
              label: 'Scene RMS',
              value: NodeTelemetryFormat.rms(telemetry.sceneRms),
            ),
            _DetailMetric(
              label: 'Sample rate',
              value: NodeTelemetryFormat.compact(
                telemetry.effectiveSampleRateHz,
                suffix: ' Hz',
              ),
            ),
            _StateMetric(
              label: 'Sample rate status',
              value: _booleanLabel(telemetry.sampleRateOk, 'OK', 'Warning'),
              color: _stateColor(colors, telemetry.sampleRateOk),
            ),
            _StateMetric(
              label: 'HPF',
              value: _hpfLabel(telemetry),
              color: _stateColor(colors, telemetry.hpfEnabled),
            ),
            _DetailMetric(
              label: 'Raw gain',
              value: NodeTelemetryFormat.gain(telemetry.micSoftwareGain),
            ),
            _DetailMetric(
              label: 'Scene gain',
              value: NodeTelemetryFormat.gain(telemetry.sceneSoftwareGain),
            ),
            _StateMetric(
              label: 'Scene clipping',
              value: telemetry.sceneClippedSampleCount?.toString() ?? 'Unknown',
              color: telemetry.sceneClippedSampleCount == null
                  ? colors.outline
                  : telemetry.sceneClippedSampleCount == 0
                  ? colors.primary
                  : colors.error,
            ),
            _StateMetric(
              label: 'Scene metrics',
              value: _booleanLabel(
                telemetry.sceneMetricsValid,
                'Valid',
                'Invalid',
              ),
              color: _stateColor(colors, telemetry.sceneMetricsValid),
            ),
          ],
        ),
        if (audioError != null && audioError.isNotEmpty) ...[
          const SizedBox(height: 6),
          Text(
            pollingWarning
                ? 'I2S polling warnings'
                : 'Audio error: $audioError',
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
              color: pollingWarning ? colors.tertiary : colors.error,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ],
    );
  }

  static String _booleanLabel(bool? value, String yes, String no) =>
      value == null
      ? 'Unknown'
      : value
      ? yes
      : no;

  static Color _stateColor(ColorScheme colors, bool? value) => value == null
      ? colors.outline
      : value
      ? colors.primary
      : colors.error;

  static String _hpfLabel(NodeTelemetry telemetry) {
    final state = _booleanLabel(telemetry.hpfEnabled, 'Enabled', 'Disabled');
    final details = <String>[];
    if (telemetry.hpfCutoffHz != null) {
      details.add(
        NodeTelemetryFormat.compact(telemetry.hpfCutoffHz, suffix: ' Hz'),
      );
    }
    if (telemetry.hpfOrder != null) {
      details.add('${telemetry.hpfOrder}th order');
    }
    return details.isEmpty
        ? state
        : '$state \u00b7 ${details.join(' \u00b7 ')}';
  }
}

class _StateMetric extends StatelessWidget {
  const _StateMetric({
    required this.label,
    required this.value,
    required this.color,
  });

  final String label;
  final String value;
  final Color color;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: 180,
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(Icons.circle, size: 7, color: color),
        const SizedBox(width: 5),
        Flexible(
          child: Text(
            '$label: $value',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
          ),
        ),
      ],
    ),
  );
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
