// SDACS Flutter component: Flutter UI implementation for the node config form portion of the SDACS operator workflow.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

class NodeConfigForm extends StatelessWidget {
  const NodeConfigForm({
    super.key,
    required this.backendIpController,
    required this.backendPortController,
    required this.nodeCountController,
  });

  final TextEditingController backendIpController;
  final TextEditingController backendPortController;
  final TextEditingController nodeCountController;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        TextFormField(
          controller: backendIpController,
          keyboardType: TextInputType.url,
          decoration: const InputDecoration(labelText: 'Backend IP'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: backendPortController,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: 'Backend port'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: nodeCountController,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: 'Number of nodes'),
        ),
      ],
    );
  }
}
