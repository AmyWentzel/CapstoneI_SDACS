import 'package:flutter/material.dart';

class SplGraph extends StatelessWidget {
  const SplGraph({super.key});

  @override
  Widget build(BuildContext context) {
    return const _GraphPlaceholder(
      title: 'SPL over time',
      icon: Icons.volume_up_outlined,
    );
  }
}

class _GraphPlaceholder extends StatelessWidget {
  const _GraphPlaceholder({required this.title, required this.icon});

  final String title;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: SizedBox(
        height: 180,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 40),
              const SizedBox(height: 8),
              Text(title, style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 4),
              const Text('Graph placeholder'),
            ],
          ),
        ),
      ),
    );
  }
}
