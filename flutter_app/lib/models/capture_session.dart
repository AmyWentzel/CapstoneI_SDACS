import 'capture_node_metric.dart';

class CaptureSession {
  const CaptureSession({
    required this.captureId,
    required this.status,
    required this.requestedAt,
    required this.scheduledStartAt,
    this.scheduledStartProvided = true,
    required this.durationSeconds,
    required this.completedNodes,
    required this.missingNodes,
    this.processingStage,
    this.modelStatus,
    this.failureStage,
    this.failureReason,
    this.updatedAt,
  });

  final String captureId;
  final String status;
  final DateTime requestedAt;
  final DateTime scheduledStartAt;
  final bool scheduledStartProvided;
  final int durationSeconds;
  final List<String> completedNodes;
  final List<String> missingNodes;
  final String? processingStage;
  final String? modelStatus;
  final String? failureStage;
  final String? failureReason;
  final DateTime? updatedAt;

  String get sessionId => captureId;
  bool get isTerminal => const {
    'complete',
    'partial',
    'failed',
    'acoustic_only',
    'processing_failed',
    'collection_failed',
  }.contains(status);

  factory CaptureSession.fromJson(Map<String, dynamic> json) {
    List<String> strings(String key) =>
        (json[key] as List<dynamic>? ?? const [])
            .map((value) => value.toString())
            .toList();
    final requested = json['requested_at']?.toString();
    final scheduled = json['scheduled_start_at']?.toString();
    return CaptureSession(
      captureId:
          json['capture_id']?.toString() ??
          json['request_id']?.toString() ??
          'unknown',
      status: json['status']?.toString() ?? 'requested',
      requestedAt: DateTime.tryParse(requested ?? '') ?? DateTime.now(),
      scheduledStartAt: DateTime.tryParse(scheduled ?? '') ?? DateTime.now(),
      scheduledStartProvided: scheduled != null && scheduled.isNotEmpty,
      durationSeconds: (json['record_seconds'] as num?)?.toInt() ?? 0,
      completedNodes: strings('completed_nodes'),
      missingNodes: strings('missing_nodes'),
      processingStage: json['processing_stage']?.toString(),
      modelStatus: json['model_status']?.toString(),
      failureStage:
          json['failure_stage']?.toString() ?? json['error_stage']?.toString(),
      failureReason:
          json['failure_reason']?.toString() ??
          json['error_message']?.toString() ??
          json['detail']?.toString(),
      updatedAt: DateTime.tryParse(json['updated_at']?.toString() ?? ''),
    );
  }

  factory CaptureSession.fromCaptureStartResponse(Map<String, dynamic> json) =>
      CaptureSession.fromJson(json);
}

class CaptureCombinedResult {
  const CaptureCombinedResult(this.json);
  final Map<String, dynamic> json;
  String get captureId => json['capture_id']?.toString() ?? 'unknown';
  Map<String, dynamic> get acoustic => _map('acoustic_analysis');
  Map<String, dynamic> get edgeImpulse => _map('edge_impulse');
  Map<String, dynamic> get fusion => _map('fusion');
  Map<String, dynamic> get recommendation => _map('recommendation');
  DateTime? get generatedAt =>
      DateTime.tryParse(acoustic['generated_at']?.toString() ?? '');
  AcousticAnalysis get acousticAnalysis => AcousticAnalysis.fromJson(acoustic);
  EdgeImpulseResult get edgeImpulseResult =>
      EdgeImpulseResult.fromJson(edgeImpulse);
  Map<String, CaptureNodeMetric> get nodeMetrics {
    final raw = acoustic['node_metrics'];
    if (raw is! Map) return const {};
    return raw.map((key, value) {
      final json = (value as Map).map(
        (nestedKey, nestedValue) => MapEntry(nestedKey.toString(), nestedValue),
      );
      final metric = CaptureNodeMetric.fromJson(json);
      return MapEntry(_normalizeNodeId(metric.nodeId), metric);
    });
  }

  static String _normalizeNodeId(String value) {
    final digits = RegExp(r'([1-4])$').firstMatch(value.trim())?.group(1);
    return digits == null ? value.trim().toLowerCase() : 'node0$digits';
  }

  Map<String, dynamic> _map(String key) =>
      (json[key] as Map?)?.map(
        (key, value) => MapEntry(key.toString(), value),
      ) ??
      {};
}

class AcousticAnalysis {
  const AcousticAnalysis({
    required this.status,
    required this.nodesUsed,
    required this.missingNodes,
    required this.nodeMetrics,
    required this.warnings,
    this.generatedAt,
    this.meanEstimatedSplDb,
    this.dominantBand,
    this.dominantFrequencyHz,
    this.loudestNodeId,
    this.quietestNodeId,
    this.spatialVariationDb,
    this.plotFilename,
  });

  final String status;
  final DateTime? generatedAt;
  final List<String> nodesUsed;
  final List<String> missingNodes;
  final Map<String, CaptureNodeMetric> nodeMetrics;
  final double? meanEstimatedSplDb;
  final String? dominantBand;
  final double? dominantFrequencyHz;
  final String? loudestNodeId;
  final String? quietestNodeId;
  final double? spatialVariationDb;
  final String? plotFilename;
  final List<String> warnings;

  bool get hasUsableMetrics => nodeMetrics.values.any(
    (metric) =>
        metric.estimatedSplDb != null ||
        metric.peakFrequencyHz != null ||
        metric.rms != null ||
        metric.dbfs != null,
  );
  bool get isSuccessful =>
      const {'complete', 'partial', 'acoustic_only'}.contains(status) &&
      hasUsableMetrics;

  factory AcousticAnalysis.fromJson(Map<String, dynamic> json) {
    double? number(String key) => (json[key] as num?)?.toDouble();
    List<String> strings(String key) =>
        (json[key] as List? ?? const []).map((value) => '$value').toList();
    final metrics = <String, CaptureNodeMetric>{};
    final rawMetrics = json['node_metrics'];
    if (rawMetrics is Map) {
      for (final entry in rawMetrics.entries) {
        if (entry.value is! Map) continue;
        final row = (entry.value as Map).map(
          (key, value) => MapEntry('$key', value),
        );
        final metric = CaptureNodeMetric.fromJson({
          'node_id': row['node_id'] ?? entry.key,
          ...row,
        });
        metrics[CaptureCombinedResult._normalizeNodeId(metric.nodeId)] = metric;
      }
    }
    return AcousticAnalysis(
      status: json['status']?.toString() ?? 'pending',
      generatedAt: DateTime.tryParse(json['generated_at']?.toString() ?? ''),
      nodesUsed: strings('nodes_used'),
      missingNodes: strings('missing_nodes'),
      nodeMetrics: Map.unmodifiable(metrics),
      meanEstimatedSplDb: number('mean_estimated_spl_db'),
      dominantBand: json['dominant_band']?.toString(),
      dominantFrequencyHz: number('dominant_frequency_hz'),
      loudestNodeId: json['loudest_node_id']?.toString(),
      quietestNodeId: json['quietest_node_id']?.toString(),
      spatialVariationDb: number('spatial_variation_db'),
      plotFilename: json['plot_filename']?.toString(),
      warnings: strings('warnings'),
    );
  }
}

class EdgeImpulseResult {
  const EdgeImpulseResult({
    required this.status,
    required this.scores,
    required this.warnings,
    this.predictedLabel,
    this.confidence,
    this.modelVersion,
  });

  final String status;
  final String? predictedLabel;
  final double? confidence;
  final Map<String, double> scores;
  final String? modelVersion;
  final List<String> warnings;

  bool get isUnavailable => const {
    'disabled',
    'model_not_configured',
    'model_unavailable',
    'unavailable',
  }.contains(status);

  factory EdgeImpulseResult.fromJson(Map<String, dynamic> json) {
    final scores = <String, double>{};
    final rawScores = json['scores'];
    if (rawScores is Map) {
      for (final entry in rawScores.entries) {
        if (entry.value is num) {
          scores['${entry.key}'] = (entry.value as num).toDouble();
        }
      }
    }
    return EdgeImpulseResult(
      status: json['status']?.toString() ?? 'unavailable',
      predictedLabel: json['predicted_label']?.toString(),
      confidence: (json['confidence'] as num?)?.toDouble(),
      scores: Map.unmodifiable(scores),
      modelVersion: json['model_version']?.toString(),
      warnings: (json['warnings'] as List? ?? const [])
          .map((value) => '$value')
          .toList(),
    );
  }
}
