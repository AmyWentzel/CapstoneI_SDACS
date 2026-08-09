// SDACS Flutter verification: regression coverage for widget test.
// These tests protect operator-visible behavior during the final branch merge.

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/acoustic_map_model.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';
import 'package:flutter_test_app/shared/sdacs_capture_labels.dart';

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

  test('AcousticMapResult parses the Raspberry Pi API contract', () {
    final result = AcousticMapResult.fromJson({
      'run_id': 'capture_20260708190829',
      'selected_label': 'high',
      'selected_label_display': 'high (4k-20kHz)',
      'available_labels': ['high'],
      'label_catalog': [
        {
          'id': 'high',
          'display_name': 'high (4k-20kHz)',
          'description': 'High-frequency test condition.',
          'has_data': true,
        },
      ],
      'nodes': [
        {
          'node_id': 'node01',
          'x_m': 0.0,
          'y_m': 0.0,
          'height_m': 1.2,
          'low_ratio': 0.57,
          'mid_ratio': 0.32,
          'high_ratio': 0.11,
          'intensity': 0.57,
          'dominant_band': 'low',
          'rows': 1,
        },
      ],
    });

    expect(result.runId, 'capture_20260708190829');
    expect(result.selectedLabel, 'high');
    expect(result.nodes, hasLength(1));
    expect(result.nodes.single.nodeId, 'node01');
    expect(result.nodes.single.lowRatio, closeTo(0.57, 0.0001));
    expect(result.labelCatalog.single.hasData, isTrue);
  });

  test('SDACS capture label IDs remain stable', () {
    expect(sdacsCaptureLabels.map((label) => label.id), [
      'noisy',
      'speech',
      'low',
      'mid',
      'high',
      'quiet_room_white_noise',
    ]);
  });
}
