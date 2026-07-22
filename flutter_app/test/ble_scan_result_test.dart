import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/ble_scan_result.dart';

void main() {
  test('parses a valid BLE scan independently from Wi-Fi RSSI', () {
    final result = BleScanResult.fromJson({
      'request_id': 'ble_scan_1',
      'status': 'complete',
      'started_at': '2026-07-22T12:00:00Z',
      'completed_at': '2026-07-22T12:00:08Z',
      'scan_duration_seconds': 8,
      'detected_count': 1,
      'nodes': [
        {
          'node_id': 'node01',
          'ble_rssi_dbm': -57,
          'address': 'AA:BB:CC:DD:EE:01',
          'local_name': 'SDACS-node01',
          'seen': true,
        },
      ],
    });
    expect(result.nodes.single.bleRssiDbm, -57);
    expect(result.nodes.single.nodeId, 'node01');
  });

  test('missing nodes are absent instead of becoming zero dBm', () {
    final result = BleScanResult.fromJson({
      'request_id': 'ble_scan_2',
      'status': 'complete',
      'started_at': '2026-07-22T12:00:00Z',
      'completed_at': '2026-07-22T12:00:08Z',
      'scan_duration_seconds': 8,
      'detected_count': 0,
      'nodes': <Object>[],
    });
    expect(result.nodes, isEmpty);
  });
}
