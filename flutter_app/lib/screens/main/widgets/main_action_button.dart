// SDACS Flutter component: Flutter UI implementation for the main action button portion of the SDACS operator workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class MainActionButton extends StatelessWidget {
  const MainActionButton({
    super.key,
    required this.icon,
    required this.label,
    required this.onPressed,
  });

  final IconData icon;
  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return FilledButton.icon(
      onPressed: onPressed,
      icon: Icon(icon),
      label: Text(label),
    );
  }
}
