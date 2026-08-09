// SDACS Flutter verification: regression coverage for node telemetry scene test.
// These tests protect operator-visible behavior during the final branch merge.

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';

void main() {
  test('parses scene telemetry promoted to the top level', () {
    final telemetry = NodeTelemetry.fromJson({
      'node_id': 'node03',
      'scene_dbfs': -64.26,
      'scene_rms': 0.000612,
      'scene_db_spl': 42,
      'scene_peak_db_spl': 51.5,
      'scene_f_peak_hz': 375,
      'scene_f_peak_acoustic_hz': 380.5,
      'scene_fft_low_ratio': 0.2,
      'scene_fft_mid_ratio': 0.5,
      'scene_fft_high_ratio': 0.3,
      'scene_fft_total_energy': 1234,
      'effective_sample_rate_hz': 47872,
      'sample_rate_ok': true,
      'scene_metrics_valid': true,
      'hpf_enabled': true,
      'scene_clipped_sample_count': 0,
      'scene_post_gain_peak_abs': 12345,
      'hpf_order': 4,
      'hpf_cutoff_hz': 150,
      'mic_software_gain': 8,
      'scene_software_gain': 16,
      'audio_error': 'i2s_timeouts',
    });

    expect(telemetry.sceneDbfs, -64.26);
    expect(telemetry.sceneRms, 0.000612);
    expect(telemetry.sceneDbSpl, 42);
    expect(telemetry.scenePeakDbSpl, 51.5);
    expect(telemetry.scenePeakFrequencyHz, 375);
    expect(telemetry.sceneAcousticPeakFrequencyHz, 380.5);
    expect(telemetry.sceneFftLowRatio, 0.2);
    expect(telemetry.sceneFftMidRatio, 0.5);
    expect(telemetry.sceneFftHighRatio, 0.3);
    expect(telemetry.sceneFftTotalEnergy, 1234);
    expect(telemetry.effectiveSampleRateHz, 47872.0);
    expect(telemetry.sampleRateOk, isTrue);
    expect(telemetry.sceneMetricsValid, isTrue);
    expect(telemetry.hpfEnabled, isTrue);
    expect(telemetry.sceneClippedSampleCount, 0);
    expect(telemetry.scenePostGainPeakAbs, 12345);
    expect(telemetry.hpfOrder, 4);
    expect(telemetry.hpfCutoffHz, 150.0);
    expect(telemetry.micSoftwareGain, 8.0);
    expect(telemetry.sceneSoftwareGain, 16.0);
    expect(telemetry.audioError, 'i2s_timeouts');
  });

  test('falls back to raw_json while top-level non-null values win', () {
    final telemetry = NodeTelemetry.fromJson({
      'node_id': 'node03',
      'scene_dbfs': -60,
      'hpf_enabled': null,
      'raw_json': {
        'scene_dbfs': -70,
        'scene_rms': '0.000612',
        'hpf_enabled': 'true',
        'scene_hpf_cutoff_hz': 150,
        'scene_hpf_order': 4.0,
      },
    });

    expect(telemetry.sceneDbfs, -60);
    expect(telemetry.sceneRms, 0.000612);
    expect(telemetry.hpfEnabled, isTrue);
    expect(telemetry.hpfCutoffHz, 150);
    expect(telemetry.hpfOrder, 4);
  });

  test('accepts legacy numeric and string boolean representations', () {
    expect(NodeTelemetry.fromJson({'sample_rate_ok': 1}).sampleRateOk, isTrue);
    expect(NodeTelemetry.fromJson({'sample_rate_ok': 0}).sampleRateOk, isFalse);
    expect(
      NodeTelemetry.fromJson({'sample_rate_ok': 'true'}).sampleRateOk,
      isTrue,
    );
    expect(
      NodeTelemetry.fromJson({'sample_rate_ok': 'false'}).sampleRateOk,
      isFalse,
    );
    expect(NodeTelemetry.fromJson({'hpf_order': '4'}).hpfOrder, 4);
    expect(NodeTelemetry.fromJson({'hpf_order': 4.0}).hpfOrder, 4);
    expect(NodeTelemetry.fromJson({'hpf_order': 4.5}).hpfOrder, isNull);
  });

  test('older payload and missing raw_json parse without scene values', () {
    final telemetry = NodeTelemetry.fromJson({
      'node_id': 'node03',
      'dbfs': -68.0,
      'capture_state': 'capturing',
    });

    expect(telemetry.dbfs, -68);
    expect(telemetry.captureState, 'capturing');
    expect(telemetry.sceneDbfs, isNull);
    expect(telemetry.sampleRateOk, isNull);
    expect(telemetry.audioError, isNull);
  });
}
