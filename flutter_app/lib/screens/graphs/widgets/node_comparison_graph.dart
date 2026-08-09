// SDACS Flutter component: Flutter UI implementation for the node comparison graph portion of the SDACS operator workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class NodeComparisonGraph extends StatelessWidget {
  const NodeComparisonGraph({super.key});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: SizedBox(
        height: 180,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.compare_arrows, size: 40),
              const SizedBox(height: 8),
              Text(
                'Node comparison',
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
