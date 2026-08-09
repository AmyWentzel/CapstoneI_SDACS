// SDACS Flutter component: Reusable SDACS UI widget: sdacs status badge.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class SdacsStatusBadge extends StatelessWidget {
  const SdacsStatusBadge({super.key, required this.status});

  final String status;

  @override
  Widget build(BuildContext context) {
    final normalizedStatus = status.toLowerCase();
    final color = switch (normalizedStatus) {
      'online' || 'ready' => Colors.green,
      'running' => Colors.blue,
      'error' => Colors.red,
      _ => Colors.grey,
    };

    return DecoratedBox(
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: color.withValues(alpha: 0.45)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
        child: Text(
          status.toUpperCase(),
          style: Theme.of(context).textTheme.labelSmall?.copyWith(
            color: color,
            fontWeight: FontWeight.w700,
          ),
        ),
      ),
    );
  }
}
