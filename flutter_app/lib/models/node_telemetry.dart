import 'dart:convert';

class NodeTelemetry {
  const NodeTelemetry({
    required this.nodeId,
    required this.status,
    required this.captureState,
    required this.rms,
    required this.dbfs,
    required this.dbSpl,
    required this.peakFrequencyHz,
    required this.p2pRaw,
    required this.zeros,
    required this.fftLowRatio,
    required this.fftMidRatio,
    required this.fftHighRatio,
    required this.fftTotalEnergy,
    required this.temperatureC,
    required this.humidityPercent,
    required this.batterySoc,
    required this.batteryVoltageV,
    required this.batteryChargeRatePctPerHr,
    required this.batteryValid,
    required this.firmwareVersion,
    required this.seq,
    required this.n,
    required this.timestamp,
  });

  final String nodeId;
  final String status;
  final String captureState;
  final double rms;
  final double dbfs;
  final double dbSpl;
  final double peakFrequencyHz;
  final double p2pRaw;
  final int zeros;
  final double fftLowRatio;
  final double fftMidRatio;
  final double fftHighRatio;
  final double fftTotalEnergy;
  final double temperatureC;
  final double humidityPercent;
  final double batterySoc;
  final double batteryVoltageV;
  final double batteryChargeRatePctPerHr;
  final bool batteryValid;
  final String firmwareVersion;
  final int seq;
  final int n;
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
      p2pRaw: _doubleValue(source, const ['p2pRaw', 'p2p_raw']),
      zeros: _intValue(source, const ['zeros']),
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
      batteryChargeRatePctPerHr: _doubleValue(source, const [
        'batteryChargeRatePctPerHr',
        'batt_charge_rate_pct_per_hr',
      ]),
      batteryValid:
          _boolValue(source, const ['batteryValid', 'batt_valid']) ?? false,
      firmwareVersion:
          _stringValue(source, const ['firmwareVersion', 'fw_version']) ??
          'unknown',
      seq: _intValue(source, const ['seq']),
      n: _intValue(source, const ['n']),
      timestamp: _dateTimeValue(source, const [
        'timestamp_iso',
        'timestamp',
        'last_seen_iso',
        'last_seen',
      ]),
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
      p2pRaw: 0,
      zeros: 0,
      fftLowRatio: 0.25,
      fftMidRatio: 0.5,
      fftHighRatio: 0.25,
      fftTotalEnergy: 1.0,
      temperatureC: 22.0 + (nodeNumber * 0.2),
      humidityPercent: 45.0 + nodeNumber,
      batterySoc: 92.0 - nodeNumber,
      batteryVoltageV: 4.0,
      batteryChargeRatePctPerHr: 0,
      batteryValid: true,
      firmwareVersion: '0.1.0',
      seq: 0,
      n: 0,
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
    final root = _stringKeyMap(json);
    final source = <String, dynamic>{...root};

    final rawJson = root['raw_json'];
    final rawMap = _decodeRawJson(rawJson);
    if (rawMap != null) {
      source.addAll(rawMap);
    }

    for (final nestedKey in const [
      'latest',
      'latest_features',
      'latest_status',
    ]) {
      final nested = source[nestedKey] ?? root[nestedKey];
      final nestedMap = _decodeRawJson(nested);
      if (nestedMap != null) {
        source.addAll(nestedMap);
      }
    }
    return source;
  }

  static Map<String, dynamic> _stringKeyMap(Map<dynamic, dynamic> map) {
    return map.map((key, value) => MapEntry(key.toString(), value));
  }

  static Map<String, dynamic>? _decodeRawJson(dynamic value) {
    if (value is Map<String, dynamic>) {
      return _stringKeyMap(value);
    }
    if (value is Map) {
      return _stringKeyMap(value);
    }
    if (value is String && value.trim().isNotEmpty) {
      try {
        final decoded = jsonDecode(value);
        if (decoded is Map) {
          return _stringKeyMap(decoded);
        }
      } on FormatException {
        return null;
      }
    }
    return null;
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

  static int _intValue(Map<String, dynamic> json, List<String> keys) {
    for (final key in keys) {
      final value = json[key];
      if (value is int) {
        return value;
      }
      if (value is num) {
        return value.toInt();
      }
      if (value is String) {
        return int.tryParse(value) ?? double.tryParse(value)?.toInt() ?? 0;
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
      if (value is num) {
        final milliseconds = value > 100000000000
            ? value.toInt()
            : value * 1000;
        return DateTime.fromMillisecondsSinceEpoch(milliseconds.toInt());
      }
    }
    return DateTime.now();
  }
}
