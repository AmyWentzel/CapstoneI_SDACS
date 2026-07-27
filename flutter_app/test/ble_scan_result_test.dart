import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/models/ble_scan_result.dart';
import 'package:flutter_test_app/models/node_telemetry.dart';

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

  test('scan normalizes and merges four nodes without replacing live data', () {
    final nodes = {
      for (var index = 1; index <= 4; index++)
        'node0$index': NodeTelemetry.fromJson({
          'node_id': 'node0$index',
          'temp_c': 20 + index,
          'rh_percent': 40 + index,
          'rssi_dbm': -40 - index,
          'fw_version': 'ota-enable-v2',
        }),
    };
    final result = BleScanResult.fromJson({
      'request_id': 'ble_scan_3',
      'status': 'complete',
      'started_at': '2026-07-22T12:00:00Z',
      'completed_at': '2026-07-22T12:00:08Z',
      'scan_duration_seconds': 8,
      'detected_count': 4,
      'nodes': [
        for (var index = 1; index <= 4; index++)
          {
            'node_id': 'SDACS-node0$index',
            'ble_rssi_dbm': -60 - index,
            'address': 'AA:BB:CC:DD:EE:0$index',
            'local_name': 'SDACS-node0$index',
            'seen': true,
          },
      ],
    });

    final merged = mergeBleScanIntoLiveNodes(nodes, result);
    expect(merged.keys, containsAll(['node01', 'node02', 'node03', 'node04']));
    expect(merged['node01']!.bleRssiDbm, -61);
    expect(merged['node04']!.bleRssiDbm, -64);
    expect(merged['node01']!.wifiRssiDbm, -41);
    expect(merged['node01']!.temperatureC, 21);
    expect(merged['node01']!.humidityPercent, 41);
    expect(merged['node01']!.firmwareVersion, 'ota-enable-v2');
    expect(merged['node01']!.bleAddress, 'AA:BB:CC:DD:EE:01');
    expect(merged['node01']!.bleScanTimestamp, isNotNull);
  });

  test('failed or stale scans can preserve the last successful BLE state', () {
    const current = NodeTelemetry(nodeId: 'node01', bleRssiDbm: -67);
    final unchangedAfterFailure = <String, NodeTelemetry>{'node01': current};
    expect(unchangedAfterFailure['node01']!.bleRssiDbm, -67);
    expect(isCurrentBleScanRequest(1, 2), isFalse);
    expect(isCurrentBleScanRequest(2, 2), isTrue);
  });
}
