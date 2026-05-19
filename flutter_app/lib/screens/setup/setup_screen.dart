import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../widgets/sdacs_app_bar.dart';
import 'widgets/node_config_form.dart';
import 'widgets/room_size_form.dart';
import 'widgets/wifi_credentials_form.dart';

class SetupScreen extends StatefulWidget {
  const SetupScreen({super.key});

  @override
  State<SetupScreen> createState() => _SetupScreenState();
}

class _SetupScreenState extends State<SetupScreen> {
  final _backendIpController = TextEditingController(
    text: BackendConfig.defaultBackendIp,
  );
  final _lengthController = TextEditingController(text: '6.0');
  final _widthController = TextEditingController(text: '4.0');
  final _heightController = TextEditingController(text: '2.5');
  final _wallMaterialController = TextEditingController(text: 'Drywall');
  final _nodeCountController = TextEditingController(text: '4');
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();

  @override
  void dispose() {
    _backendIpController.dispose();
    _lengthController.dispose();
    _widthController.dispose();
    _heightController.dispose();
    _wallMaterialController.dispose();
    _nodeCountController.dispose();
    _ssidController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  void _saveSetup() {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Setup saved locally for this prototype.')),
    );
    Navigator.of(
      context,
    ).pushNamedAndRemoveUntil(AppRoutes.main, (route) => false);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: const SdacsAppBar(title: 'Setup'),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(
            'Backend and Nodes',
            style: Theme.of(context).textTheme.titleLarge,
          ),
          const SizedBox(height: 12),
          NodeConfigForm(
            backendIpController: _backendIpController,
            nodeCountController: _nodeCountController,
          ),
          const SizedBox(height: 24),
          Text('Room', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          RoomSizeForm(
            lengthController: _lengthController,
            widthController: _widthController,
            heightController: _heightController,
            wallMaterialController: _wallMaterialController,
          ),
          const SizedBox(height: 24),
          Text('Wi-Fi', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          WifiCredentialsForm(
            ssidController: _ssidController,
            passwordController: _passwordController,
          ),
          const SizedBox(height: 24),
          FilledButton.icon(
            onPressed: _saveSetup,
            icon: const Icon(Icons.save_outlined),
            label: const Text('Save Setup'),
          ),
        ],
      ),
    );
  }
}
