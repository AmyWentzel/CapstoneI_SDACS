import 'dart:async';
import 'package:flutter/material.dart';

import '../../../models/capture_node_metric.dart';
import '../../../models/node_telemetry.dart';
import '../../../models/room_layout.dart';
import '../../../services/sdacs_api_service.dart';

class EditableRoomLayout extends StatefulWidget {
  const EditableRoomLayout({
    super.key,
    required this.nodes,
    required this.api,
    required this.onDirtyChanged,
    required this.captureMetrics,
    required this.latestCaptureId,
  });

  final List<NodeTelemetry> nodes;
  final SdacsApiService api;
  final ValueChanged<bool> onDirtyChanged;
  final Map<String, CaptureNodeMetric> captureMetrics;
  final String? latestCaptureId;

  @override
  State<EditableRoomLayout> createState() => _EditableRoomLayoutState();
}

class _EditableRoomLayoutState extends State<EditableRoomLayout> {
  static const editNodeCardSize = Size(148, 112);
  static const normalNodeCardSize = Size(148, 124);
  static const editSourceCardSize = Size(132, 92);
  static const normalSourceCardSize = Size(132, 120);
  RoomLayout _saved = RoomLayout.defaults();
  RoomLayout _draft = RoomLayout.defaults();
  bool _editing = false;
  bool _dirty = false;
  bool _saving = false;
  String? _selected;
  String? _error;
  Size get _nodeCardSize => _editing ? editNodeCardSize : normalNodeCardSize;
  Size get _sourceCardSize =>
      _editing ? editSourceCardSize : normalSourceCardSize;

  @override
  void initState() {
    super.initState();
    unawaited(_load());
  }

  Future<void> _load() async {
    try {
      final layout = await widget.api.getRoomLayout();
      if (mounted) setState(() => _saved = _draft = layout);
    } on SdacsApiException catch (error) {
      if (mounted) setState(() => _error = error.message);
    }
  }

  void _setDirty(bool value) => widget.onDirtyChanged(value);

  void _beginEdit() {
    setState(() {
      _editing = true;
      _selected = roomNodeIds.first;
      _dirty = false;
      _error = null;
    });
  }

  void _cancel() {
    setState(() {
      _draft = _saved;
      _editing = false;
      _selected = null;
      _dirty = false;
      _error = null;
    });
    _setDirty(false);
  }

  void _reset() {
    setState(() {
      final defaults = RoomLayout.defaults();
      _draft = _draft.copyWith(
        positions: defaults.positions,
        sourcePosition: defaults.sourcePosition,
      );
      _dirty = true;
      _error = null;
    });
    _setDirty(true);
  }

  Future<void> _save() async {
    setState(() => _saving = true);
    try {
      final saved = await widget.api.saveRoomLayout(_draft);
      if (!mounted) return;
      setState(() {
        _saved = _draft = saved;
        _editing = false;
        _selected = null;
        _dirty = false;
        _error = null;
      });
      _setDirty(false);
    } on SdacsApiException catch (error) {
      if (mounted) setState(() => _error = error.message);
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  bool _validDrop(String id, SpatialPosition candidate, Size canvas) {
    final candidateSize = id == 'source' ? _sourceCardSize : _nodeCardSize;
    final candidateRect = _rect(candidate, canvas, size: candidateSize);
    if (id != 'source') {
      final sourceRect = _rect(
        _draft.sourcePosition,
        canvas,
        size: _sourceCardSize,
      );
      if (candidateRect.overlaps(sourceRect.inflate(8))) return false;
    }
    for (final entry in _draft.positions.entries) {
      if (entry.key != id &&
          candidateRect.overlaps(
            _rect(entry.value, canvas, size: _nodeCardSize).inflate(6),
          )) {
        return false;
      }
    }
    return true;
  }

  Rect _rect(SpatialPosition position, Size canvas, {required Size size}) =>
      Rect.fromCenter(
        // Keep the complete card reachable while preserving normalized values.
        center: Offset(
          (position.normalizedX * canvas.width)
              .clamp(size.width / 2, canvas.width - size.width / 2)
              .toDouble(),
          (position.normalizedY * canvas.height)
              .clamp(size.height / 2, canvas.height - size.height / 2)
              .toDouble(),
        ),
        width: size.width,
        height: size.height,
      );

  void _move(String id, Offset delta, Size canvas) {
    final old = id == 'source' ? _draft.sourcePosition : _draft.positions[id]!;
    final candidate = old.copyWith(
      normalizedX: (old.normalizedX + delta.dx / canvas.width)
          .clamp(0.0, 1.0)
          .toDouble(),
      normalizedY: (old.normalizedY + delta.dy / canvas.height)
          .clamp(0.0, 1.0)
          .toDouble(),
    );
    if (!_validDrop(id, candidate, canvas)) return;
    setState(() {
      _draft = id == 'source'
          ? _draft.copyWith(sourcePosition: candidate)
          : _draft.copyWith(positions: {..._draft.positions, id: candidate});
      _selected = id;
      _dirty = true;
      _error = null;
    });
    _setDirty(true);
  }

  void _setExact(String id, String axis, String text, Size canvas) {
    final value = double.tryParse(text);
    if (value == null || !value.isFinite || value < 0 || value > 1) {
      setState(() => _error = '$axis must be between 0.00 and 1.00.');
      return;
    }
    final old = id == 'source' ? _draft.sourcePosition : _draft.positions[id]!;
    final candidate = axis == 'X'
        ? old.copyWith(normalizedX: value)
        : old.copyWith(normalizedY: value);
    if (!_validDrop(id, candidate, canvas)) {
      setState(
        () =>
            _error = 'That position overlaps another card or the Test Source.',
      );
      return;
    }
    setState(() {
      _draft = id == 'source'
          ? _draft.copyWith(sourcePosition: candidate)
          : _draft.copyWith(positions: {..._draft.positions, id: candidate});
      _dirty = true;
      _error = null;
    });
    _setDirty(true);
  }

  void _setHeight(String id, String text) {
    final value = double.tryParse(text);
    if (value == null || !value.isFinite || value < 0 || value > 20) {
      setState(() => _error = 'Height must be between 0 and 20 m.');
      return;
    }
    setState(() {
      if (id == 'source') {
        _draft = _draft.copyWith(
          sourcePosition: _draft.sourcePosition.copyWith(zM: value),
        );
      } else {
        _draft = _draft.copyWith(
          positions: {
            ..._draft.positions,
            id: _draft.positions[id]!.copyWith(zM: value),
          },
        );
      }
      _dirty = true;
      _error = null;
    });
    _setDirty(true);
  }

  void _setDimension(String axis, String text) {
    final value = double.tryParse(text);
    if (value == null || !value.isFinite || value <= 0 || value > 1000) {
      setState(
        () => _error = '$axis must be greater than zero and at most 1000 m.',
      );
      return;
    }
    setState(() {
      _draft = axis == 'Width'
          ? _draft.copyWith(roomWidthM: value)
          : _draft.copyWith(roomDepthM: value);
      _dirty = true;
      _error = null;
    });
    _setDirty(true);
  }

  @override
  Widget build(BuildContext context) {
    final byId = {for (final node in widget.nodes) node.nodeId: node};
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: const Color(0xFF12121A),
        borderRadius: BorderRadius.circular(32),
      ),
      child: Column(
        children: [
          Row(
            children: [
              const Expanded(
                child: Text(
                  'Room Node Layout',
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 20,
                    fontWeight: FontWeight.w900,
                  ),
                ),
              ),
              if (!_editing)
                OutlinedButton.icon(
                  onPressed: _beginEdit,
                  icon: const Icon(Icons.edit),
                  label: const Text('Edit Layout'),
                ),
              if (_editing) ...[
                TextButton(
                  onPressed: _saving ? null : _cancel,
                  child: const Text('Cancel'),
                ),
                TextButton(
                  onPressed: _saving ? null : _reset,
                  child: const Text('Reset to Default'),
                ),
                FilledButton.icon(
                  onPressed: _saving ? null : _save,
                  icon: const Icon(Icons.save),
                  label: const Text('Save Layout'),
                ),
              ],
            ],
          ),
          if (_editing) ...[
            const Align(
              alignment: Alignment.centerLeft,
              child: Text(
                'Drag nodes to match their approximate physical positions. Enter room dimensions for coordinates in meters.',
                style: TextStyle(color: Color(0xFFA1A1AA)),
              ),
            ),
            if (_dirty)
              const Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  'Unsaved layout changes',
                  style: TextStyle(
                    color: Colors.amber,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
          ],
          if (_error != null)
            Align(
              alignment: Alignment.centerLeft,
              child: Text(
                _error!,
                style: const TextStyle(color: Colors.redAccent),
              ),
            ),
          const SizedBox(height: 12),
          LayoutBuilder(
            builder: (context, constraints) {
              final height = constraints.maxWidth < 620 ? 620.0 : 480.0;
              final canvas = Size(constraints.maxWidth, height);
              return Column(
                children: [
                  SizedBox(
                    height: height,
                    child: DecoratedBox(
                      decoration: BoxDecoration(
                        color: const Color(0xFF1A1A25),
                        borderRadius: BorderRadius.circular(28),
                        border: Border.all(color: const Color(0x668B5CF6)),
                      ),
                      child: Stack(
                        children: [
                          Positioned.fromRect(
                            rect: _rect(
                              _draft.sourcePosition,
                              canvas,
                              size: _sourceCardSize,
                            ),
                            child: Semantics(
                              button: _editing,
                              selected: _selected == 'source',
                              label: 'Test Source layout position',
                              child: GestureDetector(
                                behavior: HitTestBehavior.opaque,
                                onTap: _editing
                                    ? () => setState(() => _selected = 'source')
                                    : null,
                                onPanUpdate: _editing
                                    ? (details) =>
                                          _move('source', details.delta, canvas)
                                    : null,
                                child: _SourceCard(
                                  selected: _selected == 'source',
                                  editing: _editing,
                                ),
                              ),
                            ),
                          ),
                          for (final id in roomNodeIds)
                            Positioned.fromRect(
                              rect: _rect(
                                _draft.positions[id]!,
                                canvas,
                                size: _nodeCardSize,
                              ),
                              child: Semantics(
                                button: _editing,
                                selected: _selected == id,
                                label: '$id layout position',
                                child: GestureDetector(
                                  behavior: HitTestBehavior.opaque,
                                  onTap: _editing
                                      ? () => setState(() => _selected = id)
                                      : null,
                                  onPanUpdate: _editing
                                      ? (details) =>
                                            _move(id, details.delta, canvas)
                                      : null,
                                  child: _LayoutNodeCard(
                                    id: id,
                                    node: byId[id],
                                    captureMetric: widget.captureMetrics[id],
                                    hasCompletedCapture:
                                        widget.latestCaptureId != null,
                                    selected: _selected == id,
                                    editing: _editing,
                                  ),
                                ),
                              ),
                            ),
                        ],
                      ),
                    ),
                  ),
                  if (_editing)
                    _EditorPanel(
                      layout: _draft,
                      selected: _selected!,
                      onX: (value) => _setExact(_selected!, 'X', value, canvas),
                      onY: (value) => _setExact(_selected!, 'Y', value, canvas),
                      onWidth: (value) => _setDimension('Width', value),
                      onDepth: (value) => _setDimension('Depth', value),
                      onHeight: (value) => _setHeight(_selected!, value),
                      node: _selected == 'source' ? null : byId[_selected],
                    ),
                ],
              );
            },
          ),
          const SizedBox(height: 12),
          const _Recommendation(),
        ],
      ),
    );
  }
}

class _LayoutNodeCard extends StatelessWidget {
  const _LayoutNodeCard({
    required this.id,
    required this.node,
    required this.captureMetric,
    required this.hasCompletedCapture,
    required this.selected,
    required this.editing,
  });
  final String id;
  final NodeTelemetry? node;
  final CaptureNodeMetric? captureMetric;
  final bool hasCompletedCapture;
  final bool selected;
  final bool editing;
  bool get _hasSpl => captureMetric?.estimatedSplDb?.isFinite == true;
  bool get _hasPeak => captureMetric?.peakFrequencyHz?.isFinite == true;
  String get _peak {
    if (!_hasPeak) return 'Peak: No data';
    final value = captureMetric!.peakFrequencyHz!;
    return value >= 1000
        ? 'Peak: ${(value / 1000).toStringAsFixed(2)} kHz'
        : 'Peak: ${value.toStringAsFixed(0)} Hz';
  }

  String get _band {
    return '';
  }

  @override
  Widget build(BuildContext context) => Container(
    key: ValueKey('layout-$id'),
    padding: const EdgeInsets.all(10),
    decoration: BoxDecoration(
      color: const Color(0xFF08080C),
      borderRadius: BorderRadius.circular(20),
      border: Border.all(
        color: selected ? Colors.amber : const Color(0xFFA78BFA),
        width: selected ? 3 : 1,
      ),
    ),
    child: editing
        ? Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                Icons.open_with,
                size: 22,
                color: selected ? Colors.amber : const Color(0xFFA78BFA),
              ),
              const SizedBox(height: 3),
              Text(
                id,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 2),
              Text(
                node?.status.toLowerCase() == 'offline'
                    ? 'Offline · Drag to position'
                    : 'Online · Drag to position',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: Color(0xFFA1A1AA), fontSize: 11),
              ),
            ],
          )
        : Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text(
                id,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
              Text(
                captureMetric == null && hasCompletedCapture
                    ? 'No capture data'
                    : _hasSpl
                    ? '${captureMetric!.estimatedSplDb!.toStringAsFixed(1)} dB est. SPL'
                    : 'Est. SPL: —',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: Colors.white),
              ),
              Text(
                '$_peak${_band.isEmpty ? '' : ' · $_band'}',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: Color(0xFFA1A1AA), fontSize: 11),
              ),
              Text(
                node?.bleRssiDbm == null
                    ? 'BLE RSSI: No reading'
                    : 'BLE RSSI: ${node!.bleRssiDbm} dBm',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: Color(0xFFA1A1AA), fontSize: 11),
              ),
            ],
          ),
  );
}

class _SourceCard extends StatelessWidget {
  const _SourceCard({required this.selected, required this.editing});
  final bool selected;
  final bool editing;
  @override
  Widget build(BuildContext context) => Container(
    key: const ValueKey('layout-source'),
    decoration: BoxDecoration(
      color: const Color(0xFF08080C),
      borderRadius: BorderRadius.circular(22),
      border: Border.all(
        color: selected ? Colors.amber : const Color(0xFF8B5CF6),
        width: selected ? 3 : 2,
      ),
    ),
    child: Column(
      mainAxisSize: MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Icon(
          editing ? Icons.open_with : Icons.speaker_group_outlined,
          color: selected ? Colors.amber : const Color(0xFFA78BFA),
        ),
        const Text(
          'Test Source',
          style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
        ),
        Text(
          editing ? 'Drag studio monitor' : 'Configured source',
          textAlign: TextAlign.center,
          style: const TextStyle(color: Color(0xFFA1A1AA), fontSize: 11),
        ),
        if (!editing)
          const Text(
            'Broker',
            textAlign: TextAlign.center,
            style: TextStyle(color: Color(0xFFA1A1AA), fontSize: 11),
          ),
      ],
    ),
  );
}

class _EditorPanel extends StatelessWidget {
  const _EditorPanel({
    required this.layout,
    required this.selected,
    required this.onX,
    required this.onY,
    required this.onWidth,
    required this.onDepth,
    required this.onHeight,
    required this.node,
  });
  final RoomLayout layout;
  final String selected;
  final ValueChanged<String> onX, onY, onWidth, onDepth, onHeight;
  final NodeTelemetry? node;
  @override
  Widget build(BuildContext context) {
    final isSource = selected == 'source';
    final position = isSource
        ? layout.sourcePosition
        : layout.positions[selected]!;
    final distance = !isSource && layout.hasDimensions
        ? layout.layoutDistance(selected)
        : null;
    final sourceDistances = isSource && layout.hasDimensions
        ? roomNodeIds
              .map(
                (id) => '$id ${layout.layoutDistance(id).toStringAsFixed(2)} m',
              )
              .join(', ')
        : null;
    Widget field(String label, double? value, ValueChanged<String> submit) =>
        SizedBox(
          width: 135,
          child: TextFormField(
            key: ValueKey('$selected-$label-${value?.toStringAsFixed(3)}'),
            initialValue: value?.toStringAsFixed(2) ?? '',
            decoration: InputDecoration(labelText: label),
            keyboardType: const TextInputType.numberWithOptions(decimal: true),
            onFieldSubmitted: submit,
          ),
        );
    return Padding(
      padding: const EdgeInsets.only(top: 12),
      child: Wrap(
        spacing: 12,
        runSpacing: 8,
        crossAxisAlignment: WrapCrossAlignment.center,
        children: [
          Text(
            'Selected: ${isSource ? 'Test Source' : selected}',
            style: const TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.bold,
            ),
          ),
          field('Normalized X', position.normalizedX, onX),
          field('Normalized Y', position.normalizedY, onY),
          field('Room width (m)', layout.roomWidthM, onWidth),
          field('Room depth (m)', layout.roomDepthM, onDepth),
          field('Height Z (m)', position.zM, onHeight),
          Text(
            layout.hasDimensions
                ? 'X ${(position.normalizedX * layout.roomWidthM!).toStringAsFixed(2)} m, Y ${(position.normalizedY * layout.roomDepthM!).toStringAsFixed(2)} m · ${isSource ? 'Configured layout distances: $sourceDistances' : 'Configured layout distance ${distance!.toStringAsFixed(2)} m'}'
                : 'Enter room dimensions for physical coordinates.',
            style: const TextStyle(color: Color(0xFFA1A1AA)),
          ),
          if (!isSource)
            Text(
              _nodeDetails(node),
              style: const TextStyle(color: Color(0xFFA1A1AA), fontSize: 12),
            ),
        ],
      ),
    );
  }

  static String _nodeDetails(NodeTelemetry? node) {
    final fresh =
        node != null &&
        node.status.toLowerCase() != 'offline' &&
        node.timestamp != null &&
        DateTime.now().difference(node.timestamp!).abs() <=
            const Duration(minutes: 2);
    final spl = fresh && node.dbSpl?.isFinite == true && node.dbSpl! > 0
        ? '${node.dbSpl!.toStringAsFixed(1)} dB est. SPL'
        : 'Est. SPL —';
    final peak =
        fresh &&
            node.peakFrequencyHz?.isFinite == true &&
            node.peakFrequencyHz! > 0
        ? node.peakFrequencyHz! >= 1000
              ? '${(node.peakFrequencyHz! / 1000).toStringAsFixed(2)} kHz'
              : '${node.peakFrequencyHz!.toStringAsFixed(0)} Hz'
        : 'No data';
    final ratios = node == null
        ? const <String, double>{}
        : {
            'Low': node.fftLowRatio,
            'Mid': node.fftMidRatio,
            'High': node.fftHighRatio,
          };
    final availableRatios = ratios.entries
        .where((entry) => entry.value?.isFinite == true)
        .toList();
    final band = availableRatios.any((entry) => entry.value! > 0)
        ? availableRatios.reduce((a, b) => a.value! >= b.value! ? a : b).key
        : 'No data';
    final battery =
        node?.batteryValid == true &&
            node!.batterySoc != null &&
            node.batterySoc! > 0
        ? '${node.batterySoc!.toStringAsFixed(0)}%'
        : 'No data';
    return 'Live telemetry: $spl · Peak $peak · $band · '
        '${node?.bleRssiDbm == null ? 'BLE not scanned' : 'BLE ${node!.bleRssiDbm} dBm'} · '
        'Battery $battery · ${fresh ? 'Online' : 'Stale/offline'}';
  }
}

class _Recommendation extends StatelessWidget {
  const _Recommendation();
  @override
  Widget build(BuildContext context) => const ListTile(
    leading: Icon(Icons.lightbulb_outline, color: Color(0xFFA78BFA)),
    title: Text(
      'Recommendation',
      style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
    ),
    subtitle: Text(
      'Run a Capture to generate capture-specific acoustic guidance.',
      style: TextStyle(color: Color(0xFFA1A1AA)),
    ),
  );
}
