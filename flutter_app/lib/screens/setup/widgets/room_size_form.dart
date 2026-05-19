import 'package:flutter/material.dart';

class RoomSizeForm extends StatelessWidget {
  const RoomSizeForm({
    super.key,
    required this.lengthController,
    required this.widthController,
    required this.heightController,
    required this.wallMaterialController,
  });

  final TextEditingController lengthController;
  final TextEditingController widthController;
  final TextEditingController heightController;
  final TextEditingController wallMaterialController;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        TextFormField(
          controller: lengthController,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: 'Room length (m)'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: widthController,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: 'Room width (m)'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: heightController,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: 'Room height (m)'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: wallMaterialController,
          decoration: const InputDecoration(labelText: 'Wall material'),
        ),
      ],
    );
  }
}
