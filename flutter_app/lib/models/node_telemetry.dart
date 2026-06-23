class NodeTelemetry {
  const NodeTelemetry({
    required this.nodeId,
    required this.status,
    required this.captureState,
    required this.rms,
    required this.dbfs,
    required this.dbSpl,
    required this.peakFrequencyHz,
    required this.fftLowRatio,
    required this.fftMidRatio,
    required this.fftHighRatio,
    required this.fftTotalEnergy,
    required this.temperatureC,
    required this.humidityPercent,
    required this.batterySoc,
    required this.batteryVoltageV,
    required this.batteryValid,
    required this.firmwareVersion,
    required this.timestamp,
  });

  final String nodeId;
  final String status;
  final String captureState;
  final double rms;
  final double dbfs;
  final double dbSpl;
  final double peakFrequencyHz;
  final double fftLowRatio;
  final double fftMidRatio;
  final double fftHighRatio;
  final double fftTotalEnergy;
  final double temperatureC;
  final double humidityPercent;
  final double batterySoc;
  final double batteryVoltageV;
  final bool batteryValid;
  final String firmwareVersion;
  final DateTime timestamp;

  factory NodeTelemetry.fromJson(Map<String, dynamic> json) {
    final source = _telemetrySource(json);

    return NodeTelemetry(
      nodeId: _stringValue(source, const ['nodeId', 'node_id']) ?? 'unknown',
      status: _stringValue(source, const ['status']) ?? 'online',
      captureState:
          _stringValue(source, const ['captureState', 'capture_state']) ??
          'unknown',
      rms: _doubleValue(source, const ['rms']),
      dbfs: _doubleValue(source, const ['dbfs']),
      dbSpl: _doubleValue(source, const ['dbSpl', 'db_spl']),
      peakFrequencyHz: _doubleValue(source, const [
        'peakFrequencyHz',
        'f_peak_hz',
        'peak_hz',
      ]),
      fftLowRatio: _doubleValue(source, const ['fftLowRatio', 'fft_low_ratio']),
      fftMidRatio: _doubleValue(source, const ['fftMidRatio', 'fft_mid_ratio']),
      fftHighRatio: _doubleValue(source, const [
        'fftHighRatio',
        'fft_high_ratio',
      ]),
      fftTotalEnergy: _doubleValue(source, const [
        'fftTotalEnergy',
        'fft_total_energy',
      ]),
      temperatureC: _doubleValue(source, const ['temperatureC', 'temp_c']),
      humidityPercent: _doubleValue(source, const [
        'humidityPercent',
        'rh_percent',
      ]),
      batterySoc: _doubleValue(source, const [
        'batterySoc',
        'batt_soc_percent',
      ]),
      batteryVoltageV: _doubleValue(source, const [
        'batteryVoltageV',
        'batt_voltage_v',
      ]),
      batteryValid:
          _boolValue(source, const ['batteryValid', 'batt_valid']) ?? false,
      firmwareVersion:
          _stringValue(source, const ['firmwareVersion', 'fw_version']) ??
          'unknown',
      timestamp: _dateTimeValue(source, const ['timestamp', 'timestamp_iso']),
    );
  }

  factory NodeTelemetry.mock(String nodeId, {String status = 'online'}) {
    final nodeNumber =
        int.tryParse(nodeId.replaceAll(RegExp('[^0-9]'), '')) ?? 1;
    return NodeTelemetry(
      nodeId: nodeId,
      status: status,
      captureState: 'idle',
      rms: 0.12 + (nodeNumber * 0.01),
      dbfs: -24.5 + nodeNumber,
      dbSpl: 62.0 + nodeNumber,
      peakFrequencyHz: 1000 + (nodeNumber * 50),
      fftLowRatio: 0.25,
      fftMidRatio: 0.5,
      fftHighRatio: 0.25,
      fftTotalEnergy: 1.0,
      temperatureC: 22.0 + (nodeNumber * 0.2),
      humidityPercent: 45.0 + nodeNumber,
      batterySoc: 92.0 - nodeNumber,
      batteryVoltageV: 4.0,
      batteryValid: true,
      firmwareVersion: '0.1.0',
      timestamp: DateTime.now(),
    );
  }

  static List<NodeTelemetry> mockList() {
    return const [
      'node01',
      'node02',
      'node03',
      'node04',
    ].map(NodeTelemetry.mock).toList();
  }

  static Map<String, dynamic> _telemetrySource(Map<String, dynamic> json) {
    final source = <String, dynamic>{...json};
    for (final nestedKey in const [
      'latest',
      'latest_features',
      'latest_status',
    ]) {
      final nested = json[nestedKey];
      if (nested is Map<String, dynamic>) {
        source.addAll(nested);
      }
    }
    return source;
  }

  static String? _stringValue(Map<String, dynamic> json, List<String> keys) {
    for (final key in keys) {
      final value = json[key];
      if (value != null) {
        return value.toString();
      }
    }
    return null;
  }

  static double _doubleValue(Map<String, dynamic> json, List<String> keys) {
    for (final key in keys) {
      final value = json[key];
      if (value is num) {
        return value.toDouble();
      }
      if (value is String) {
        return double.tryParse(value) ?? 0;
      }
    }
    return 0;
  }

  static bool? _boolValue(Map<String, dynamic> json, List<String> keys) {
    for (final key in keys) {
      final value = json[key];
      if (value is bool) {
        return value;
      }
      if (value is String) {
        return value.toLowerCase() == 'true';
      }
      if (value is num) {
        return value != 0;
      }
    }
    return null;
  }

  static DateTime _dateTimeValue(Map<String, dynamic> json, List<String> keys) {
    for (final key in keys) {
      final value = json[key];
      if (value is String) {
        final parsed = DateTime.tryParse(value);
        if (parsed != null) {
          return parsed;
        }
      }
    }
    return DateTime.now();
  }
}
