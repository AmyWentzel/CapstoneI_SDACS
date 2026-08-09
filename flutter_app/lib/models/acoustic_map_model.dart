// SDACS Flutter component: Typed client models for acoustic-map labels, node values, recommendations, and rendered results.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

class AcousticMapLabelStatus {
  const AcousticMapLabelStatus({
    required this.id,
    required this.displayName,
    required this.description,
    required this.hasData,
  });

  final String id;
  final String displayName;
  final String description;
  final bool hasData;

  factory AcousticMapLabelStatus.fromJson(Map<String, dynamic> json) {
    return AcousticMapLabelStatus(
      id: json['id']?.toString() ?? 'unknown',
      displayName: json['display_name']?.toString() ?? 'Unknown',
      description: json['description']?.toString() ?? '',
      hasData: json['has_data'] == true,
    );
  }
}

class AcousticMapNode {
  const AcousticMapNode({
    required this.nodeId,
    required this.xM,
    required this.yM,
    required this.heightM,
    required this.lowRatio,
    required this.midRatio,
    required this.highRatio,
    required this.intensity,
    required this.dominantBand,
    required this.rows,
  });

  final String nodeId;
  final double xM;
  final double yM;
  final double heightM;
  final double lowRatio;
  final double midRatio;
  final double highRatio;
  final double intensity;
  final String dominantBand;
  final int rows;

  factory AcousticMapNode.fromJson(Map<String, dynamic> json) {
    double asDouble(String key) => (json[key] as num?)?.toDouble() ?? 0;
    return AcousticMapNode(
      nodeId: json['node_id']?.toString() ?? 'unknown',
      xM: asDouble('x_m'),
      yM: asDouble('y_m'),
      heightM: asDouble('height_m'),
      lowRatio: asDouble('low_ratio'),
      midRatio: asDouble('mid_ratio'),
      highRatio: asDouble('high_ratio'),
      intensity: asDouble('intensity'),
      dominantBand: json['dominant_band']?.toString() ?? 'unknown',
      rows: (json['rows'] as num?)?.toInt() ?? 0,
    );
  }
}

class AcousticMapResult {
  const AcousticMapResult({
    required this.runId,
    required this.selectedLabel,
    required this.selectedLabelDisplay,
    required this.availableLabels,
    required this.labelCatalog,
    required this.nodes,
  });

  final String runId;
  final String selectedLabel;
  final String selectedLabelDisplay;
  final List<String> availableLabels;
  final List<AcousticMapLabelStatus> labelCatalog;
  final List<AcousticMapNode> nodes;

  factory AcousticMapResult.fromJson(Map<String, dynamic> json) {
    final rawNodes = json['nodes'] as List<dynamic>? ?? const [];
    final rawLabels = json['available_labels'] as List<dynamic>? ?? const [];
    final rawCatalog = json['label_catalog'] as List<dynamic>? ?? const [];
    return AcousticMapResult(
      runId: json['run_id']?.toString() ?? 'unknown',
      selectedLabel: json['selected_label']?.toString() ?? 'latest',
      selectedLabelDisplay:
          json['selected_label_display']?.toString() ??
          json['selected_label']?.toString() ??
          'Latest',
      availableLabels: rawLabels.map((value) => value.toString()).toList(),
      labelCatalog: rawCatalog
          .whereType<Map>()
          .map(
            (value) => AcousticMapLabelStatus.fromJson(
              value.map((key, item) => MapEntry(key.toString(), item)),
            ),
          )
          .toList(),
      nodes: rawNodes
          .whereType<Map>()
          .map(
            (value) => AcousticMapNode.fromJson(
              value.map((key, item) => MapEntry(key.toString(), item)),
            ),
          )
          .toList(),
    );
  }
}
