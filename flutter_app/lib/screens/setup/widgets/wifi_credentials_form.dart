import 'package:flutter/material.dart';

class WifiCredentialsForm extends StatelessWidget {
  const WifiCredentialsForm({
    super.key,
    required this.ssidController,
    required this.passwordController,
  });

  final TextEditingController ssidController;
  final TextEditingController passwordController;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        TextFormField(
          controller: ssidController,
          decoration: const InputDecoration(labelText: 'Wi-Fi SSID'),
        ),
        const SizedBox(height: 12),
        TextFormField(
          controller: passwordController,
          obscureText: true,
          decoration: const InputDecoration(labelText: 'Wi-Fi password'),
        ),
      ],
    );
  }
}
