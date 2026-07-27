class CaptureNodeMetric {
  const CaptureNodeMetric({
    required this.nodeId,
    required this.sampleCount,
    this.excludedSampleCount,
    this.rms,
    this.dbfs,
    this.estimatedSplDb,
    this.peakFrequencyHz,
    this.normalizedX,
    this.normalizedY,
    this.xPositionM,
    this.yPositionM,
    this.zPositionM,
    this.warnings = const [],
  });

  final String nodeId;
  final int? sampleCount;
  final int? excludedSampleCount;
  final double? rms;
  final double? dbfs;
  final double? estimatedSplDb;
  final double? peakFrequencyHz;
  final double? normalizedX;
  final double? normalizedY;
  final double? xPositionM;
  final double? yPositionM;
  final double? zPositionM;
  final List<String> warnings;

  factory CaptureNodeMetric.fromJson(Map<String, dynamic> json) {
    double? number(String key) => (json[key] as num?)?.toDouble();
    return CaptureNodeMetric(
      nodeId: json['node_id']?.toString() ?? 'unknown',
      sampleCount: (json['sample_count'] as num?)?.toInt(),
      excludedSampleCount: (json['excluded_sample_count'] as num?)?.toInt(),
      rms: number('mean_rms'),
      dbfs: number('mean_dbfs'),
      estimatedSplDb: number('mean_estimated_spl_db'),
      peakFrequencyHz: number('peak_frequency_hz'),
      normalizedX: number('normalized_x'),
      normalizedY: number('normalized_y'),
      xPositionM: number('x_position_m'),
      yPositionM: number('y_position_m'),
      zPositionM: number('z_position_m'),
      warnings: (json['warnings'] as List? ?? const [])
          .map((value) => value.toString())
          .toList(),
    );
  }
}
