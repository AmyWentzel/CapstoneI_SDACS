import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import 'widgets/node_config_form.dart';
import 'widgets/room_size_form.dart';
import 'widgets/wifi_credentials_form.dart';

class SetupScreen extends StatefulWidget {
  const SetupScreen({super.key});

  @override
  State<SetupScreen> createState() => _SetupScreenState();
}

class _SetupScreenState extends State<SetupScreen> {
  static const background = Color(0xFF08080C);
  static const panel = Color(0xFF12121A);
  static const panelLight = Color(0xFF1A1A25);
  static const accent = Color(0xFF8B5CF6);
  static const accentLight = Color(0xFFA78BFA);
  static const textMuted = Color(0xFFA1A1AA);

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

    Navigator.of(context).pushNamedAndRemoveUntil(
      AppRoutes.main,
      (route) => false,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Theme(
      data: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: background,
        colorScheme: const ColorScheme.dark(
          primary: accent,
          secondary: accentLight,
          surface: panel,
        ),
        inputDecorationTheme: InputDecorationTheme(
          filled: true,
          fillColor: panelLight,
          labelStyle: const TextStyle(color: textMuted),
          hintStyle: const TextStyle(color: textMuted),
          enabledBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
            borderSide: BorderSide(color: accent.withOpacity(0.25)),
          ),
          focusedBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
            borderSide: const BorderSide(color: accentLight, width: 1.4),
          ),
          border: OutlineInputBorder(
            borderRadius: BorderRadius.circular(16),
          ),
        ),
      ),
      child: Scaffold(
        appBar: AppBar(
          title: const Text(
            'SETUP',
            style: TextStyle(
              fontWeight: FontWeight.w900,
              letterSpacing: 1.6,
              fontSize: 15,
            ),
          ),
          centerTitle: true,
          backgroundColor: background,
          foregroundColor: Colors.white,
          elevation: 0,
        ),
        body: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 10, 24, 32),
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 1200),
              child: Column(
                children: [
                  const _SetupHero(),
                  const SizedBox(height: 28),
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final isWide = constraints.maxWidth >= 950;

                      if (isWide) {
                        return Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(
                              child: _SetupCard(
                                icon: Icons.hub_outlined,
                                title: 'Backend and Nodes',
                                subtitle:
                                    'Configure the Raspberry Pi backend and number of SDACS nodes.',
                                child: NodeConfigForm(
                                  backendIpController: _backendIpController,
                                  nodeCountController: _nodeCountController,
                                ),
                              ),
                            ),
                            const SizedBox(width: 22),
                            Expanded(
                              child: _SetupCard(
                                icon: Icons.meeting_room_outlined,
                                title: 'Room Profile',
                                subtitle:
                                    'Enter the physical room dimensions and wall material.',
                                child: RoomSizeForm(
                                  lengthController: _lengthController,
                                  widthController: _widthController,
                                  heightController: _heightController,
                                  wallMaterialController:
                                      _wallMaterialController,
                                ),
                              ),
                            ),
                          ],
                        );
                      }

                      return Column(
                        children: [
                          _SetupCard(
                            icon: Icons.hub_outlined,
                            title: 'Backend and Nodes',
                            subtitle:
                                'Configure the Raspberry Pi backend and number of SDACS nodes.',
                            child: NodeConfigForm(
                              backendIpController: _backendIpController,
                              nodeCountController: _nodeCountController,
                            ),
                          ),
                          const SizedBox(height: 22),
                          _SetupCard(
                            icon: Icons.meeting_room_outlined,
                            title: 'Room Profile',
                            subtitle:
                                'Enter the physical room dimensions and wall material.',
                            child: RoomSizeForm(
                              lengthController: _lengthController,
                              widthController: _widthController,
                              heightController: _heightController,
                              wallMaterialController: _wallMaterialController,
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 22),
                  _SetupCard(
                    icon: Icons.wifi_rounded,
                    title: 'Wi-Fi Credentials',
                    subtitle:
                        'Provide network information so the nodes can communicate with the backend.',
                    child: WifiCredentialsForm(
                      ssidController: _ssidController,
                      passwordController: _passwordController,
                    ),
                  ),
                  const SizedBox(height: 28),
                  SizedBox(
                    height: 54,
                    width: 260,
                    child: FilledButton.icon(
                      style: FilledButton.styleFrom(
                        backgroundColor: accent,
                        foregroundColor: Colors.white,
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(18),
                        ),
                      ),
                      onPressed: _saveSetup,
                      icon: const Icon(Icons.save_outlined),
                      label: const Text(
                        'Save Setup',
                        style: TextStyle(fontWeight: FontWeight.w800),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _SetupHero extends StatelessWidget {
  const _SetupHero();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 38),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(34),
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Color(0xFF181020),
            Color(0xFF101018),
            Color(0xFF08080C),
          ],
        ),
      ),
      child: Column(
        children: [
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: _SetupScreenState.accent.withOpacity(0.14),
              shape: BoxShape.circle,
            ),
            child: const Icon(
              Icons.tune_rounded,
              color: _SetupScreenState.accentLight,
              size: 40,
            ),
          ),
          const SizedBox(height: 20),
          Text(
            'System Setup',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.displaySmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w900,
                ),
          ),
          const SizedBox(height: 12),
          const Text(
            'Configure the room, node network, Wi-Fi credentials, and backend connection before running acoustic calibration.',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: _SetupScreenState.textMuted,
              fontSize: 15,
              height: 1.5,
            ),
          ),
        ],
      ),
    );
  }
}

class _SetupCard extends StatelessWidget {
  const _SetupCard({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.child,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: _SetupScreenState.panel,
        borderRadius: BorderRadius.circular(30),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                padding: const EdgeInsets.all(11),
                decoration: BoxDecoration(
                  color: _SetupScreenState.accent.withOpacity(0.12),
                  borderRadius: BorderRadius.circular(16),
                ),
                child: Icon(
                  icon,
                  color: _SetupScreenState.accentLight,
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Text(
                  title,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 20,
                    fontWeight: FontWeight.w900,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Text(
            subtitle,
            style: const TextStyle(
              color: _SetupScreenState.textMuted,
              height: 1.4,
            ),
          ),
          const SizedBox(height: 22),
          child,
        ],
      ),
    );
  }
}