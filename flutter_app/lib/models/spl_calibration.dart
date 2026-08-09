// SDACS Flutter component: Typed request/response models for SPL calibration preview and apply workflows.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

class SplCalibrationNodePreview {
  const SplCalibrationNodePreview({
    required this.nodeId,
    required this.sampleCount,
    required this.eligible,
    required this.warnings,
    this.measuredDbfs,
    this.measuredSplDb,
    this.currentOffsetDb,
    this.suggestedOffsetDb,
    this.adjustmentDb,
    this.measurementErrorDb,
    this.representativeFrequencyHz,
    this.toneDetectedCount = 0,
    this.toneSampleCount = 0,
    this.toneDetectionRate,
    this.withinTolerance,
  });

  final String nodeId;
  final int sampleCount;
  final double? measuredDbfs;
  final double? measuredSplDb;
  final double? currentOffsetDb;
  final double? suggestedOffsetDb;
  final double? adjustmentDb;
  final double? measurementErrorDb;
  final double? representativeFrequencyHz;
  final int toneDetectedCount;
  final int toneSampleCount;
  final double? toneDetectionRate;
  final bool eligible;
  final bool? withinTolerance;
  final List<String> warnings;

  factory SplCalibrationNodePreview.fromJson(Map<String, dynamic> json) {
    double? number(String key) => (json[key] as num?)?.toDouble();
    return SplCalibrationNodePreview(
      nodeId: json['node_id']?.toString() ?? 'unknown',
      sampleCount: (json['sample_count'] as num?)?.toInt() ?? 0,
      measuredDbfs: number('measured_dbfs'),
      measuredSplDb: number('measured_spl_db'),
      currentOffsetDb: number('current_offset_db'),
      suggestedOffsetDb: number('suggested_offset_db'),
      adjustmentDb: number('adjustment_db'),
      measurementErrorDb: number('measurement_error_db'),
      representativeFrequencyHz: number('representative_frequency_hz'),
      toneDetectedCount: (json['tone_detected_count'] as num?)?.toInt() ?? 0,
      toneSampleCount: (json['tone_sample_count'] as num?)?.toInt() ?? 0,
      toneDetectionRate: number('tone_detection_rate'),
      eligible: json['eligible'] == true,
      withinTolerance: json['within_tolerance'] as bool?,
      warnings: (json['warnings'] as List? ?? const [])
          .map((value) => value.toString())
          .toList(),
    );
  }
}

class SplCalibrationPreview {
  const SplCalibrationPreview({
    required this.captureId,
    required this.referenceSplDb,
    required this.status,
    required this.toleranceDb,
    required this.generatedAt,
    required this.nodes,
    required this.warnings,
  });

  final String captureId;
  final double referenceSplDb;
  final String status;
  final double toleranceDb;
  final DateTime generatedAt;
  final List<SplCalibrationNodePreview> nodes;
  final List<String> warnings;

  bool get canApply => status == 'ready' && nodes.every((node) => node.eligible);
  bool get allWithinTolerance =>
      nodes.isNotEmpty && nodes.every((node) => node.withinTolerance == true);

  factory SplCalibrationPreview.fromJson(Map<String, dynamic> json) {
    return SplCalibrationPreview(
      captureId: json['capture_id']?.toString() ?? '',
      referenceSplDb: (json['reference_spl_db'] as num?)?.toDouble() ?? 0,
      status: json['status']?.toString() ?? 'invalid',
      toleranceDb: (json['tolerance_db'] as num?)?.toDouble() ?? 1,
      generatedAt:
          DateTime.tryParse(json['generated_at']?.toString() ?? '') ??
          DateTime.now(),
      nodes: (json['nodes'] as List? ?? const [])
          .whereType<Map>()
          .map(
            (row) => SplCalibrationNodePreview.fromJson(
              row.map((key, value) => MapEntry(key.toString(), value)),
            ),
          )
          .toList(),
      warnings: (json['warnings'] as List? ?? const [])
          .map((value) => value.toString())
          .toList(),
    );
  }
}

class SplCalibrationNodeApplyResult {
  const SplCalibrationNodeApplyResult({
    required this.nodeId,
    required this.requestedOffsetDb,
    required this.requestId,
    required this.published,
    required this.acknowledged,
    required this.applied,
    this.reportedOffsetDb,
    this.reason,
  });

  final String nodeId;
  final double requestedOffsetDb;
  final String requestId;
  final bool published;
  final bool acknowledged;
  final bool applied;
  final double? reportedOffsetDb;
  final String? reason;

  factory SplCalibrationNodeApplyResult.fromJson(Map<String, dynamic> json) {
    return SplCalibrationNodeApplyResult(
      nodeId: json['node_id']?.toString() ?? 'unknown',
      requestedOffsetDb:
          (json['requested_offset_db'] as num?)?.toDouble() ?? 0,
      requestId: json['request_id']?.toString() ?? '',
      published: json['published'] == true,
      acknowledged: json['acknowledged'] == true,
      applied: json['applied'] == true,
      reportedOffsetDb: (json['reported_offset_db'] as num?)?.toDouble(),
      reason: json['reason']?.toString(),
    );
  }
}

class SplCalibrationApplyResult {
  const SplCalibrationApplyResult({
    required this.captureId,
    required this.referenceSplDb,
    required this.status,
    required this.appliedAt,
    required this.nodes,
    required this.warnings,
  });

  final String captureId;
  final double referenceSplDb;
  final String status;
  final DateTime appliedAt;
  final List<SplCalibrationNodeApplyResult> nodes;
  final List<String> warnings;

  bool get complete => status == 'complete' && nodes.every((node) => node.applied);

  factory SplCalibrationApplyResult.fromJson(Map<String, dynamic> json) {
    return SplCalibrationApplyResult(
      captureId: json['capture_id']?.toString() ?? '',
      referenceSplDb: (json['reference_spl_db'] as num?)?.toDouble() ?? 0,
      status: json['status']?.toString() ?? 'failed',
      appliedAt:
          DateTime.tryParse(json['applied_at']?.toString() ?? '') ??
          DateTime.now(),
      nodes: (json['nodes'] as List? ?? const [])
          .whereType<Map>()
          .map(
            (row) => SplCalibrationNodeApplyResult.fromJson(
              row.map((key, value) => MapEntry(key.toString(), value)),
            ),
          )
          .toList(),
      warnings: (json['warnings'] as List? ?? const [])
          .map((value) => value.toString())
          .toList(),
    );
  }
}
