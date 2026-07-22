class BleNodeScanResult {
  const BleNodeScanResult({
    required this.nodeId,
    required this.bleRssiDbm,
    required this.address,
    required this.localName,
    required this.seen,
  });

  factory BleNodeScanResult.fromJson(Map<String, dynamic> json) {
    return BleNodeScanResult(
      nodeId: json['node_id'] as String,
      bleRssiDbm: (json['ble_rssi_dbm'] as num).toInt(),
      address: json['address'] as String,
      localName: json['local_name'] as String,
      seen: json['seen'] as bool? ?? true,
    );
  }

  final String nodeId;
  final int bleRssiDbm;
  final String address;
  final String localName;
  final bool seen;
}

class BleScanResult {
  const BleScanResult({
    required this.requestId,
    required this.status,
    required this.startedAt,
    required this.completedAt,
    required this.scanDurationSeconds,
    required this.detectedCount,
    required this.nodes,
  });

  factory BleScanResult.fromJson(Map<String, dynamic> json) {
    final nodeRows = json['nodes'];
    if (nodeRows is! List) {
      throw const FormatException('BLE scan response has no node list.');
    }
    return BleScanResult(
      requestId: json['request_id'] as String,
      status: json['status'] as String,
      startedAt: DateTime.parse(json['started_at'] as String),
      completedAt: DateTime.parse(json['completed_at'] as String),
      scanDurationSeconds: (json['scan_duration_seconds'] as num).toDouble(),
      detectedCount: (json['detected_count'] as num).toInt(),
      nodes: nodeRows
          .whereType<Map>()
          .map(
            (row) => BleNodeScanResult.fromJson(
              row.map((key, value) => MapEntry(key.toString(), value)),
            ),
          )
          .toList(growable: false),
    );
  }

  final String requestId;
  final String status;
  final DateTime startedAt;
  final DateTime completedAt;
  final double scanDurationSeconds;
  final int detectedCount;
  final List<BleNodeScanResult> nodes;
}
