import 'package:flutter/material.dart';

class FftPeakGraph extends StatelessWidget {
  const FftPeakGraph({super.key});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: SizedBox(
        height: 180,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.multiline_chart, size: 40),
              const SizedBox(height: 8),
              Text(
                'FFT peak frequency over time',
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
