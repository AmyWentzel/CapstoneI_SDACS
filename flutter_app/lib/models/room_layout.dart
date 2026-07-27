import 'dart:math' as math;

const roomNodeIds = ['node01', 'node02', 'node03', 'node04'];

class SpatialPosition {
  const SpatialPosition({
    required this.normalizedX,
    required this.normalizedY,
    this.zM = 1.2,
  });

  final double normalizedX;
  final double normalizedY;
  final double zM;

  SpatialPosition copyWith({
    double? normalizedX,
    double? normalizedY,
    double? zM,
  }) => SpatialPosition(
    normalizedX: normalizedX ?? this.normalizedX,
    normalizedY: normalizedY ?? this.normalizedY,
    zM: zM ?? this.zM,
  );

  Map<String, dynamic> toJson() => {
    'normalized_x': normalizedX,
    'normalized_y': normalizedY,
    'z_m': zM,
  };

  factory SpatialPosition.fromJson(Map<String, dynamic> json) =>
      SpatialPosition(
        normalizedX: (json['normalized_x'] as num).toDouble(),
        normalizedY: (json['normalized_y'] as num).toDouble(),
        zM: (json['z_m'] as num?)?.toDouble() ?? 1.2,
      );
}

class RoomLayout {
  const RoomLayout({
    required this.positions,
    this.layoutId = 'primary',
    this.roomWidthM,
    this.roomDepthM,
    this.sourcePosition = const SpatialPosition(
      normalizedX: .5,
      normalizedY: .5,
    ),
    this.updatedAt,
    this.revision = 0,
  });

  final String layoutId;
  final double? roomWidthM;
  final double? roomDepthM;
  final SpatialPosition sourcePosition;
  final Map<String, SpatialPosition> positions;
  final DateTime? updatedAt;
  final int revision;

  static RoomLayout defaults() => const RoomLayout(
    positions: {
      'node01': SpatialPosition(normalizedX: .16, normalizedY: .18),
      'node02': SpatialPosition(normalizedX: .84, normalizedY: .18),
      'node03': SpatialPosition(normalizedX: .16, normalizedY: .82),
      'node04': SpatialPosition(normalizedX: .84, normalizedY: .82),
    },
  );

  bool get hasDimensions => roomWidthM != null && roomDepthM != null;

  double layoutDistance(String nodeId) {
    final node = positions[nodeId]!;
    final dx = (node.normalizedX - sourcePosition.normalizedX) * roomWidthM!;
    final dy = (node.normalizedY - sourcePosition.normalizedY) * roomDepthM!;
    return math.sqrt(dx * dx + dy * dy);
  }

  RoomLayout copyWith({
    Map<String, SpatialPosition>? positions,
    SpatialPosition? sourcePosition,
    double? roomWidthM,
    double? roomDepthM,
    bool clearWidth = false,
    bool clearDepth = false,
    int? revision,
    DateTime? updatedAt,
  }) => RoomLayout(
    layoutId: layoutId,
    positions: positions ?? this.positions,
    roomWidthM: clearWidth ? null : roomWidthM ?? this.roomWidthM,
    roomDepthM: clearDepth ? null : roomDepthM ?? this.roomDepthM,
    sourcePosition: sourcePosition ?? this.sourcePosition,
    revision: revision ?? this.revision,
    updatedAt: updatedAt ?? this.updatedAt,
  );

  Map<String, dynamic> toJson() => {
    'layout_id': layoutId,
    'room_width_m': roomWidthM,
    'room_depth_m': roomDepthM,
    'source_position': sourcePosition.toJson(),
    'node_positions': roomNodeIds
        .map((id) => {'node_id': id, 'position': positions[id]!.toJson()})
        .toList(),
    'updated_at': updatedAt?.toIso8601String(),
    'revision': revision,
  };

  factory RoomLayout.fromJson(Map<String, dynamic> json) {
    final nodes = <String, SpatialPosition>{};
    for (final item in json['node_positions'] as List) {
      final row = Map<String, dynamic>.from(item as Map);
      nodes[row['node_id'] as String] = SpatialPosition.fromJson(
        Map<String, dynamic>.from(row['position'] as Map),
      );
    }
    return RoomLayout(
      layoutId: json['layout_id'] as String? ?? 'primary',
      roomWidthM: (json['room_width_m'] as num?)?.toDouble(),
      roomDepthM: (json['room_depth_m'] as num?)?.toDouble(),
      sourcePosition: SpatialPosition.fromJson(
        Map<String, dynamic>.from(json['source_position'] as Map),
      ),
      positions: nodes,
      updatedAt: DateTime.tryParse(json['updated_at']?.toString() ?? ''),
      revision: (json['revision'] as num?)?.toInt() ?? 0,
    );
  }
}
