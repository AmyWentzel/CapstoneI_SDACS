import 'dart:convert';

class NodeTelemetry {
  const NodeTelemetry({
    required this.nodeId,
    this.status = 'online',
    this.captureState = 'unknown',
    this.rms,
    this.dbfs,
    this.dbSpl,
    this.peakFrequencyHz,
    this.p2pRaw,
    this.zeros,
    this.fftLowRatio,
    this.fftMidRatio,
    this.fftHighRatio,
    this.fftTotalEnergy,
    this.sceneDbfs,
    this.sceneRms,
    this.sceneDbSpl,
    this.scenePeakDbSpl,
    this.scenePeakFrequencyHz,
    this.sceneAcousticPeakFrequencyHz,
    this.sceneFftLowRatio,
    this.sceneFftMidRatio,
    this.sceneFftHighRatio,
    this.sceneFftTotalEnergy,
    this.effectiveSampleRateHz,
    this.sampleRateOk,
    this.sceneMetricsValid,
    this.hpfEnabled,
    this.sceneClippedSampleCount,
    this.scenePostGainPeakAbs,
    this.hpfOrder,
    this.hpfCutoffHz,
    this.micSoftwareGain,
    this.sceneSoftwareGain,
    this.audioError,
    this.temperatureC,
    this.humidityPercent,
    this.batterySoc,
    this.batteryVoltageV,
    this.batteryChargeRatePctPerHr,
    this.batteryValid,
    this.firmwareVersion,
    this.wifiRssiDbm,
    this.bleRssiDbm,
    this.bleProximity,
    this.bleAddress,
    this.bleSeenCount,
    this.bleScanTimestamp,
    this.seq,
    this.n,
    this.timestamp,
    this.providedFields = const {},
  });

  final String nodeId;
  final String status;
  final String captureState;
  final double? rms;
  final double? dbfs;
  final double? dbSpl;
  final double? peakFrequencyHz;
  final double? p2pRaw;
  final int? zeros;
  final double? fftLowRatio;
  final double? fftMidRatio;
  final double? fftHighRatio;
  final double? fftTotalEnergy;
  final double? sceneDbfs;
  final double? sceneRms;
  final double? sceneDbSpl;
  final double? scenePeakDbSpl;
  final double? scenePeakFrequencyHz;
  final double? sceneAcousticPeakFrequencyHz;
  final double? sceneFftLowRatio;
  final double? sceneFftMidRatio;
  final double? sceneFftHighRatio;
  final double? sceneFftTotalEnergy;
  final double? effectiveSampleRateHz;
  final bool? sampleRateOk;
  final bool? sceneMetricsValid;
  final bool? hpfEnabled;
  final int? sceneClippedSampleCount;
  final int? scenePostGainPeakAbs;
  final int? hpfOrder;
  final double? hpfCutoffHz;
  final double? micSoftwareGain;
  final double? sceneSoftwareGain;
  final String? audioError;
  final double? temperatureC;
  final double? humidityPercent;
  final double? batterySoc;
  final double? batteryVoltageV;
  final double? batteryChargeRatePctPerHr;
  final bool? batteryValid;
  final String? firmwareVersion;
  final int? wifiRssiDbm;
  final int? bleRssiDbm;
  final String? bleProximity;
  final String? bleAddress;
  final int? bleSeenCount;
  final DateTime? bleScanTimestamp;
  final int? seq;
  final int? n;
  final DateTime? timestamp;

  /// Canonical fields explicitly present in the source record. This lets merge
  /// distinguish an omitted value from an explicitly supplied null.
  final Set<String> providedFields;

  factory NodeTelemetry.fromJson(Map<String, dynamic> json) {
    final source = _telemetrySource(json);
    final provided = <String>{};
    T? read<T>(
      String canonical,
      List<String> aliases,
      T? Function(dynamic value) parse,
    ) {
      for (final key in aliases) {
        if (source.containsKey(key)) {
          final parsed = parse(source[key]);
          if (parsed != null) provided.add(canonical);
          return parsed;
        }
      }
      return null;
    }

    final nodeId =
        read('nodeId', const ['nodeId', 'node_id'], _parseString) ?? 'unknown';
    return NodeTelemetry(
      nodeId: _normalizeNodeId(nodeId),
      status: read('status', const ['status'], _parseString) ?? 'online',
      captureState:
          read('captureState', const [
            'captureState',
            'capture_state',
          ], _parseString) ??
          'unknown',
      rms: read('rms', const ['rms'], _parseDouble),
      dbfs: read('dbfs', const ['dbfs'], _parseDouble),
      dbSpl: read('dbSpl', const ['dbSpl', 'db_spl'], _parseDouble),
      peakFrequencyHz: read('peakFrequencyHz', const [
        'peakFrequencyHz',
        'f_peak_hz',
        'peak_hz',
      ], _parseDouble),
      p2pRaw: read('p2pRaw', const ['p2pRaw', 'p2p_raw'], _parseDouble),
      zeros: read('zeros', const ['zeros'], _parseInt),
      fftLowRatio: read('fftLowRatio', const [
        'fftLowRatio',
        'fft_low_ratio',
      ], _parseDouble),
      fftMidRatio: read('fftMidRatio', const [
        'fftMidRatio',
        'fft_mid_ratio',
      ], _parseDouble),
      fftHighRatio: read('fftHighRatio', const [
        'fftHighRatio',
        'fft_high_ratio',
      ], _parseDouble),
      fftTotalEnergy: read('fftTotalEnergy', const [
        'fftTotalEnergy',
        'fft_total_energy',
      ], _parseDouble),
      sceneDbfs: read('sceneDbfs', const ['scene_dbfs'], _parseDouble),
      sceneRms: read('sceneRms', const ['scene_rms'], _parseDouble),
      sceneDbSpl: read('sceneDbSpl', const ['scene_db_spl'], _parseDouble),
      scenePeakDbSpl: read('scenePeakDbSpl', const [
        'scene_peak_db_spl',
      ], _parseDouble),
      scenePeakFrequencyHz: read('scenePeakFrequencyHz', const [
        'scene_f_peak_hz',
      ], _parseDouble),
      sceneAcousticPeakFrequencyHz: read('sceneAcousticPeakFrequencyHz', const [
        'scene_f_peak_acoustic_hz',
      ], _parseDouble),
      sceneFftLowRatio: read('sceneFftLowRatio', const [
        'scene_fft_low_ratio',
      ], _parseDouble),
      sceneFftMidRatio: read('sceneFftMidRatio', const [
        'scene_fft_mid_ratio',
      ], _parseDouble),
      sceneFftHighRatio: read('sceneFftHighRatio', const [
        'scene_fft_high_ratio',
      ], _parseDouble),
      sceneFftTotalEnergy: read('sceneFftTotalEnergy', const [
        'scene_fft_total_energy',
      ], _parseDouble),
      effectiveSampleRateHz: read('effectiveSampleRateHz', const [
        'effective_sample_rate_hz',
      ], _parseDouble),
      sampleRateOk: read('sampleRateOk', const ['sample_rate_ok'], _parseBool),
      sceneMetricsValid: read('sceneMetricsValid', const [
        'scene_metrics_valid',
      ], _parseBool),
      hpfEnabled: read('hpfEnabled', const [
        'hpf_enabled',
        'scene_hpf_enabled',
      ], _parseBool),
      sceneClippedSampleCount: read('sceneClippedSampleCount', const [
        'scene_clipped_sample_count',
      ], _parseInt),
      scenePostGainPeakAbs: read('scenePostGainPeakAbs', const [
        'scene_post_gain_peak_abs',
      ], _parseInt),
      hpfOrder: read('hpfOrder', const [
        'hpf_order',
        'scene_hpf_order',
      ], _parseInt),
      hpfCutoffHz: read('hpfCutoffHz', const [
        'hpf_cutoff_hz',
        'scene_hpf_cutoff_hz',
      ], _parseDouble),
      micSoftwareGain: read('micSoftwareGain', const [
        'mic_software_gain',
      ], _parseDouble),
      sceneSoftwareGain: read('sceneSoftwareGain', const [
        'scene_software_gain',
      ], _parseDouble),
      audioError: read('audioError', const ['audio_error'], _parseString),
      temperatureC: read('temperatureC', const [
        'temperatureC',
        'temperature_c',
        'temp_c',
      ], _parseDouble),
      humidityPercent: read('humidityPercent', const [
        'humidityPercent',
        'humidity_percent',
        'rh_percent',
      ], _parseDouble),
      batterySoc: read('batterySoc', const [
        'batterySoc',
        'battery_soc_percent',
        'soc_percent',
        'batt_soc_percent',
      ], _parseDouble),
      batteryVoltageV: read('batteryVoltageV', const [
        'batteryVoltageV',
        'battery_voltage_v',
        'batt_voltage_v',
      ], _parseDouble),
      batteryChargeRatePctPerHr: read('batteryChargeRatePctPerHr', const [
        'batteryChargeRatePctPerHr',
        'batt_charge_rate_pct_per_hr',
      ], _parseDouble),
      batteryValid: read('batteryValid', const [
        'batteryValid',
        'batt_valid',
      ], _parseBool),
      firmwareVersion: read('firmwareVersion', const [
        'firmwareVersion',
        'firmware_version',
        'firmware',
        'fw',
        'fw_version',
      ], _parseString),
      wifiRssiDbm: read('wifiRssiDbm', const [
        'wifiRssiDbm',
        'rssi_dbm',
      ], _parseInt),
      bleRssiDbm: read('bleRssiDbm', const [
        'bleRssiDbm',
        'ble_rssi_dbm',
      ], _parseInt),
      bleProximity: read('bleProximity', const [
        'bleProximity',
        'ble_proximity',
      ], _parseString),
      bleAddress: read('bleAddress', const [
        'bleAddress',
        'ble_address',
        'address',
      ], _parseString),
      bleSeenCount: read('bleSeenCount', const [
        'bleSeenCount',
        'ble_seen_count',
        'seen_count',
      ], _parseInt),
      bleScanTimestamp: read('bleScanTimestamp', const [
        'bleScanTimestamp',
        'ble_scan_timestamp',
      ], _parseDateTime),
      seq: read('seq', const ['seq'], _parseInt),
      n: read('n', const ['n'], _parseInt),
      timestamp: read('timestamp', const [
        'timestamp_iso',
        'timestamp',
        'last_seen_iso',
        'last_seen',
      ], _parseDateTime),
      providedFields: provided,
    );
  }

  NodeTelemetry merge(NodeTelemetry update) {
    T? choose<T>(String field, T? current, T? incoming) =>
        update.providedFields.contains(field) ? incoming : current;
    return NodeTelemetry(
      nodeId: update.nodeId == 'unknown' ? nodeId : update.nodeId,
      status: update.providedFields.contains('status') ? update.status : status,
      captureState: update.providedFields.contains('captureState')
          ? update.captureState
          : captureState,
      rms: choose('rms', rms, update.rms),
      dbfs: choose('dbfs', dbfs, update.dbfs),
      dbSpl: choose('dbSpl', dbSpl, update.dbSpl),
      peakFrequencyHz: choose(
        'peakFrequencyHz',
        peakFrequencyHz,
        update.peakFrequencyHz,
      ),
      p2pRaw: choose('p2pRaw', p2pRaw, update.p2pRaw),
      zeros: choose('zeros', zeros, update.zeros),
      fftLowRatio: choose('fftLowRatio', fftLowRatio, update.fftLowRatio),
      fftMidRatio: choose('fftMidRatio', fftMidRatio, update.fftMidRatio),
      fftHighRatio: choose('fftHighRatio', fftHighRatio, update.fftHighRatio),
      fftTotalEnergy: choose(
        'fftTotalEnergy',
        fftTotalEnergy,
        update.fftTotalEnergy,
      ),
      sceneDbfs: choose('sceneDbfs', sceneDbfs, update.sceneDbfs),
      sceneRms: choose('sceneRms', sceneRms, update.sceneRms),
      sceneDbSpl: choose('sceneDbSpl', sceneDbSpl, update.sceneDbSpl),
      scenePeakDbSpl: choose(
        'scenePeakDbSpl',
        scenePeakDbSpl,
        update.scenePeakDbSpl,
      ),
      scenePeakFrequencyHz: choose(
        'scenePeakFrequencyHz',
        scenePeakFrequencyHz,
        update.scenePeakFrequencyHz,
      ),
      sceneAcousticPeakFrequencyHz: choose(
        'sceneAcousticPeakFrequencyHz',
        sceneAcousticPeakFrequencyHz,
        update.sceneAcousticPeakFrequencyHz,
      ),
      sceneFftLowRatio: choose(
        'sceneFftLowRatio',
        sceneFftLowRatio,
        update.sceneFftLowRatio,
      ),
      sceneFftMidRatio: choose(
        'sceneFftMidRatio',
        sceneFftMidRatio,
        update.sceneFftMidRatio,
      ),
      sceneFftHighRatio: choose(
        'sceneFftHighRatio',
        sceneFftHighRatio,
        update.sceneFftHighRatio,
      ),
      sceneFftTotalEnergy: choose(
        'sceneFftTotalEnergy',
        sceneFftTotalEnergy,
        update.sceneFftTotalEnergy,
      ),
      effectiveSampleRateHz: choose(
        'effectiveSampleRateHz',
        effectiveSampleRateHz,
        update.effectiveSampleRateHz,
      ),
      sampleRateOk: choose('sampleRateOk', sampleRateOk, update.sampleRateOk),
      sceneMetricsValid: choose(
        'sceneMetricsValid',
        sceneMetricsValid,
        update.sceneMetricsValid,
      ),
      hpfEnabled: choose('hpfEnabled', hpfEnabled, update.hpfEnabled),
      sceneClippedSampleCount: choose(
        'sceneClippedSampleCount',
        sceneClippedSampleCount,
        update.sceneClippedSampleCount,
      ),
      scenePostGainPeakAbs: choose(
        'scenePostGainPeakAbs',
        scenePostGainPeakAbs,
        update.scenePostGainPeakAbs,
      ),
      hpfOrder: choose('hpfOrder', hpfOrder, update.hpfOrder),
      hpfCutoffHz: choose('hpfCutoffHz', hpfCutoffHz, update.hpfCutoffHz),
      micSoftwareGain: choose(
        'micSoftwareGain',
        micSoftwareGain,
        update.micSoftwareGain,
      ),
      sceneSoftwareGain: choose(
        'sceneSoftwareGain',
        sceneSoftwareGain,
        update.sceneSoftwareGain,
      ),
      audioError: choose('audioError', audioError, update.audioError),
      temperatureC: choose('temperatureC', temperatureC, update.temperatureC),
      humidityPercent: choose(
        'humidityPercent',
        humidityPercent,
        update.humidityPercent,
      ),
      batterySoc: choose('batterySoc', batterySoc, update.batterySoc),
      batteryVoltageV: choose(
        'batteryVoltageV',
        batteryVoltageV,
        update.batteryVoltageV,
      ),
      batteryChargeRatePctPerHr: choose(
        'batteryChargeRatePctPerHr',
        batteryChargeRatePctPerHr,
        update.batteryChargeRatePctPerHr,
      ),
      batteryValid: choose('batteryValid', batteryValid, update.batteryValid),
      firmwareVersion: choose(
        'firmwareVersion',
        firmwareVersion,
        update.firmwareVersion,
      ),
      wifiRssiDbm: choose('wifiRssiDbm', wifiRssiDbm, update.wifiRssiDbm),
      bleRssiDbm: choose('bleRssiDbm', bleRssiDbm, update.bleRssiDbm),
      bleProximity: choose('bleProximity', bleProximity, update.bleProximity),
      bleAddress: choose('bleAddress', bleAddress, update.bleAddress),
      bleSeenCount: choose('bleSeenCount', bleSeenCount, update.bleSeenCount),
      bleScanTimestamp: choose(
        'bleScanTimestamp',
        bleScanTimestamp,
        update.bleScanTimestamp,
      ),
      seq: choose('seq', seq, update.seq),
      n: choose('n', n, update.n),
      timestamp: choose('timestamp', timestamp, update.timestamp),
      providedFields: {...providedFields, ...update.providedFields},
    );
  }

  factory NodeTelemetry.mock(String nodeId, {String status = 'online'}) {
    final number = int.tryParse(nodeId.replaceAll(RegExp('[^0-9]'), '')) ?? 1;
    return NodeTelemetry(
      nodeId: nodeId,
      status: status,
      captureState: 'idle',
      rms: 0.12 + number * 0.01,
      dbfs: -24.5 + number,
      dbSpl: (62 + number).toDouble(),
      peakFrequencyHz: (1000 + number * 50).toDouble(),
      fftLowRatio: 0.25,
      fftMidRatio: 0.5,
      fftHighRatio: 0.25,
      fftTotalEnergy: 1,
      temperatureC: 22 + number * 0.2,
      humidityPercent: (45 + number).toDouble(),
      batterySoc: 92 - number.toDouble(),
      batteryVoltageV: 4,
      batteryValid: true,
      firmwareVersion: '0.1.0',
      timestamp: DateTime.now(),
    );
  }

  static List<NodeTelemetry> mockList() => const [
    'node01',
    'node02',
    'node03',
    'node04',
  ].map(NodeTelemetry.mock).toList();

  static Map<String, dynamic> _telemetrySource(Map<String, dynamic> json) {
    final root = _stringKeyMap(json);
    final rawMap = _decodeMap(root['raw_json']);
    // Raw firmware JSON is a compatibility fallback. Promoted API fields win.
    final source = <String, dynamic>{
      ...?rawMap,
      ...Map.fromEntries(root.entries.where((entry) => entry.value != null)),
    };
    for (final nestedKey in const [
      'latest',
      'latest_features',
      'latest_status',
    ]) {
      final nested = _decodeMap(source[nestedKey] ?? root[nestedKey]);
      if (nested != null) {
        // Aggregate REST records are full Pydantic objects, so fields absent
        // from that MQTT record type arrive as null. They must not erase a
        // value supplied by another nested record type.
        final nestedRaw = _decodeMap(nested['raw_json']);
        final normalizedNested = <String, dynamic>{...?nestedRaw, ...nested};
        source.addAll(
          Map.fromEntries(
            normalizedNested.entries.where((e) => e.value != null),
          ),
        );
      }
    }
    return source;
  }

  static Map<String, dynamic> _stringKeyMap(Map<dynamic, dynamic> map) =>
      map.map((key, value) => MapEntry(key.toString(), value));

  static Map<String, dynamic>? _decodeMap(dynamic value) {
    if (value is Map) return _stringKeyMap(value);
    if (value is String && value.trim().isNotEmpty) {
      try {
        final decoded = jsonDecode(value);
        if (decoded is Map) return _stringKeyMap(decoded);
      } on FormatException {
        return null;
      }
    }
    return null;
  }

  static String _normalizeNodeId(String value) {
    final compact = value.trim().toLowerCase().replaceAll(
      RegExp(r'[^a-z0-9]'),
      '',
    );
    final match = RegExp(r'^(?:sdacs)?node0*([1-4])$').firstMatch(compact);
    return match == null
        ? value.trim().toLowerCase()
        : 'node0${match.group(1)}';
  }

  static String? _parseString(dynamic value) =>
      value == null || value.toString().trim().isEmpty
      ? null
      : value.toString();
  static double? _parseDouble(dynamic value) {
    final parsed = value is num
        ? value.toDouble()
        : double.tryParse(value?.toString() ?? '');
    return parsed?.isFinite == true ? parsed : null;
  }

  static int? _parseInt(dynamic value) {
    if (value is int) return value;
    if (value is num && value.isFinite && value == value.truncateToDouble()) {
      return value.toInt();
    }
    final parsed = double.tryParse(value?.toString().trim() ?? '');
    return parsed != null &&
            parsed.isFinite &&
            parsed == parsed.truncateToDouble()
        ? parsed.toInt()
        : null;
  }

  static bool? _parseBool(dynamic value) {
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) {
      final normalized = value.trim().toLowerCase();
      if (normalized == 'true' || normalized == '1') return true;
      if (normalized == 'false' || normalized == '0') return false;
    }
    return null;
  }

  static DateTime? _parseDateTime(dynamic value) {
    if (value is String) return DateTime.tryParse(value);
    if (value is num && value.isFinite) {
      final milliseconds = value > 100000000000 ? value.toInt() : value * 1000;
      return DateTime.fromMillisecondsSinceEpoch(milliseconds.toInt());
    }
    return null;
  }
}
