import 'package:flutter/material.dart';

import '../../../config/backend_config.dart';
import '../../../models/acoustic_map_model.dart';
import '../../../services/acoustic_map_api_service.dart';
import '../../../shared/sdacs_capture_labels.dart';

class AcousticMapCard extends StatefulWidget {
  const AcousticMapCard({super.key});

  @override
  State<AcousticMapCard> createState() => _AcousticMapCardState();
}

class _AcousticMapCardState extends State<AcousticMapCard> {
  AcousticMapResult? _result;
  String _requestedLabel = 'latest';
  String? _error;
  bool _loading = true;
  int _cacheBust = 0;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_result == null && _loading) {
      _load();
    }
  }

  Future<void> _load([String? label]) async {
    final requestedLabel = label ?? _requestedLabel;
    setState(() {
      _requestedLabel = requestedLabel;
      _loading = true;
      _error = null;
    });
    try {
      final config = BackendConfigScope.configOf(context);
      final service = AcousticMapApiService(config: config);
      final result = await service.getMap(label: requestedLabel);
      if (!mounted) return;
      setState(() {
        _result = result;
        _cacheBust = DateTime.now().millisecondsSinceEpoch;
        _loading = false;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _result = null;
        _error = error.toString();
        _loading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final result = _result;
    final config = BackendConfigScope.configOf(context);
    final service = AcousticMapApiService(config: config);

    final dropdownItems = <DropdownMenuItem<String>>[
      const DropdownMenuItem(value: 'latest', child: Text('Latest capture')),
      ...sdacsCaptureLabels.map(
        (label) =>
            DropdownMenuItem(value: label.id, child: Text(label.displayName)),
      ),
    ];

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    '3D Frequency-Ratio Room Map',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                ),
                IconButton(
                  tooltip: 'Refresh map',
                  onPressed: _loading ? null : () => _load(),
                  icon: const Icon(Icons.refresh),
                ),
              ],
            ),
            const SizedBox(height: 8),
            DropdownButtonFormField<String>(
              initialValue: _requestedLabel,
              decoration: const InputDecoration(
                labelText: 'Capture label',
                border: OutlineInputBorder(),
              ),
              items: dropdownItems,
              onChanged: _loading
                  ? null
                  : (label) {
                      if (label != null) _load(label);
                    },
            ),
            const SizedBox(height: 8),
            if (_loading) const LinearProgressIndicator(),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: Text(
                  _error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            if (result != null && !_loading) ...[
              const SizedBox(height: 12),
              Text('Displayed label: ${result.selectedLabelDisplay}'),
              const SizedBox(height: 8),
              AspectRatio(
                aspectRatio: 3 / 2,
                child: InteractiveViewer(
                  minScale: 0.8,
                  maxScale: 4,
                  child: Image.network(
                    service
                        .imageUri(_requestedLabel, cacheBust: _cacheBust)
                        .toString(),
                    fit: BoxFit.contain,
                    errorBuilder: (context, error, stackTrace) => const Center(
                      child: Text(
                        'The backend could not render the acoustic-map image.',
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: result.nodes.map((node) {
                  return Chip(
                    label: Text(
                      '${node.nodeId}: H ${(node.highRatio * 100).round()}% '
                      'M ${(node.midRatio * 100).round()}% '
                      'L ${(node.lowRatio * 100).round()}%',
                    ),
                  );
                }).toList(),
              ),
              const SizedBox(height: 8),
              Text('Capture: ${result.runId}'),
            ],
          ],
        ),
      ),
    );
  }
}
