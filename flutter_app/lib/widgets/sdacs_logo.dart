// SDACS Flutter component: Reusable SDACS UI widget: sdacs logo.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class SdacsLogo extends StatelessWidget {
  const SdacsLogo({
    super.key,
    required this.size,
    this.showTitle = false,
    this.showFullName = false,
  });

  static const assetPath = 'assets/images/sdacs_logo_transparent.png';
  final double size;
  final bool showTitle;
  final bool showFullName;

  @override
  Widget build(BuildContext context) => Column(
    mainAxisSize: MainAxisSize.min,
    children: [
      Semantics(
        image: true,
        label: 'SDACS logo',
        child: SizedBox(
          width: size,
          height: size,
          child: Image.asset(
            assetPath,
            fit: BoxFit.contain,
            semanticLabel: 'SDACS logo',
          ),
        ),
      ),
      if (showTitle) ...[
        const SizedBox(height: 18),
        Text(
          'SDACS',
          textAlign: TextAlign.center,
          style: Theme.of(
            context,
          ).textTheme.headlineLarge?.copyWith(fontWeight: FontWeight.w800),
        ),
      ],
      if (showFullName) ...[
        const SizedBox(height: 8),
        Text(
          'Smart Distributed Acoustic Calibration System',
          textAlign: TextAlign.center,
          style: Theme.of(context).textTheme.bodyLarge,
        ),
      ],
    ],
  );
}
