class NodeTelemetry {
  const NodeTelemetry({
    required this.nodeId,
    required this.status,
    required this.rms,
    required this.dbfs,
    required this.dbSpl,
    required this.peakFrequencyHz,
    required this.temperatureC,
    required this.humidityPercent,
    required this.batterySoc,
    required this.firmwareVersion,
    required this.timestamp,
  });

  final String nodeId;
  final String status;
  final double rms;
  final double dbfs;
  final double dbSpl;
  final double peakFrequencyHz;
  final double temperatureC;
  final double humidityPercent;
  final double batterySoc;
  final String firmwareVersion;
  final DateTime timestamp;

  factory NodeTelemetry.fromJson(Map<String, dynamic> json) {
    return NodeTelemetry(
      nodeId: json['nodeId'] as String? ?? 'unknown',
      status: json['status'] as String? ?? 'offline',
      rms: (json['rms'] as num?)?.toDouble() ?? 0,
      dbfs: (json['dbfs'] as num?)?.toDouble() ?? 0,
      dbSpl: (json['dbSpl'] as num?)?.toDouble() ?? 0,
      peakFrequencyHz: (json['peakFrequencyHz'] as num?)?.toDouble() ?? 0,
      temperatureC: (json['temperatureC'] as num?)?.toDouble() ?? 0,
      humidityPercent: (json['humidityPercent'] as num?)?.toDouble() ?? 0,
      batterySoc: (json['batterySoc'] as num?)?.toDouble() ?? 0,
      firmwareVersion: json['firmwareVersion'] as String? ?? 'unknown',
      timestamp:
          DateTime.tryParse(json['timestamp'] as String? ?? '') ??
          DateTime.now(),
    );
  }

  factory NodeTelemetry.mock(String nodeId, {String status = 'online'}) {
    final nodeNumber =
        int.tryParse(nodeId.replaceAll(RegExp('[^0-9]'), '')) ?? 1;
    return NodeTelemetry(
      nodeId: nodeId,
      status: status,
      rms: 0.12 + (nodeNumber * 0.01),
      dbfs: -24.5 + nodeNumber,
      dbSpl: 62.0 + nodeNumber,
      peakFrequencyHz: 1000 + (nodeNumber * 50),
      temperatureC: 22.0 + (nodeNumber * 0.2),
      humidityPercent: 45.0 + nodeNumber,
      batterySoc: 92.0 - nodeNumber,
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
}
