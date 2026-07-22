import 'package:flutter/material.dart';

import '../../widgets/sdacs_app_bar.dart';
import 'widgets/acoustic_map_card.dart';
import 'widgets/fft_peak_graph.dart';
import 'widgets/node_comparison_graph.dart';
import 'widgets/spl_graph.dart';

class GraphsScreen extends StatelessWidget {
  const GraphsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: const SdacsAppBar(title: 'Graphs and Results'),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: const [
          AcousticMapCard(),
          SizedBox(height: 12),
          SplGraph(),
          SizedBox(height: 12),
          FftPeakGraph(),
          SizedBox(height: 12),
          _BatteryGraphPlaceholder(),
          SizedBox(height: 12),
          NodeComparisonGraph(),
        ],
      ),
    );
  }
}

class _BatteryGraphPlaceholder extends StatelessWidget {
  const _BatteryGraphPlaceholder();

  @override
  Widget build(BuildContext context) {
    return Card(
      child: SizedBox(
        height: 180,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.battery_charging_full_outlined, size: 40),
              const SizedBox(height: 8),
              Text(
                'Battery SOC over time',
                style: Theme.of(context).textTheme.titleMedium,
              ),
              const SizedBox(height: 4),
              const Text('Graph placeholder'),
            ],
          ),
        ),
      ),
    );
  }
}
