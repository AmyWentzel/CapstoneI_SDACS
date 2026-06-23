import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';

void main() {
  test('NodeTelemetry parses backend snake_case JSON', () {
    final telemetry = NodeTelemetry.fromJson({
      'node_id': 'node01',
      'status': 'online',
      'capture_state': 'idle',
      'rms': 0.42,
      'dbfs': -12.5,
      'db_spl': 71.2,
      'f_peak_hz': 997.0,
      'fft_low_ratio': 0.2,
      'fft_mid_ratio': 0.5,
      'fft_high_ratio': 0.3,
      'fft_total_energy': 123.4,
      'temp_c': 22.6,
      'rh_percent': 45.0,
      'batt_soc_percent': 88.0,
      'batt_voltage_v': 4.05,
      'batt_valid': true,
      'fw_version': '0.1.0',
      'timestamp_iso': '2026-06-23T02:43:50.704528Z',
    });

    expect(telemetry.nodeId, 'node01');
    expect(telemetry.dbSpl, 71.2);
    expect(telemetry.peakFrequencyHz, 997.0);
    expect(telemetry.batteryValid, isTrue);
  });
}
