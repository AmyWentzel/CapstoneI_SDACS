import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../services/sdacs_api_service.dart';

class LoadingScreen extends StatefulWidget {
  const LoadingScreen({super.key});

  @override
  State<LoadingScreen> createState() => _LoadingScreenState();
}

class _LoadingScreenState extends State<LoadingScreen>
    with SingleTickerProviderStateMixin {
  static const background = Color(0xFF08080C);
  static const panel = Color(0xFF12121A);
  static const accent = Color(0xFF8B5CF6);
  static const accentLight = Color(0xFFA78BFA);
  static const textMuted = Color(0xFFA1A1AA);

  final SdacsApiService _apiService = const SdacsApiService();

  late AnimationController _controller;

  @override
  void initState() {
    super.initState();

    _controller = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat();

    _startApp();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  Future<void> _startApp() async {
    await _apiService.checkBackendHealth();

    await Future<void>.delayed(
      const Duration(milliseconds: 10000),
    );

    if (!mounted) {
      return;
    }

    Navigator.of(context).pushReplacementNamed(
      AppRoutes.main,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Theme(
      data: ThemeData.dark(),
      child: Scaffold(
        backgroundColor: background,
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Container(
                  width: 130,
                  height: 130,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: accent.withOpacity(0.08),
                  ),
                  child: RotationTransition(
                    turns: Tween<double>(
                      begin: 0,
                      end: 1,
                    ).animate(_controller),
                    child: const Icon(
                      Icons.graphic_eq,
                      size: 64,
                      color: accentLight,
                    ),
                  ),
                ),

                const SizedBox(height: 36),

                Text(
                  'SDACS',
                  style: Theme.of(context).textTheme.displayMedium?.copyWith(
                        color: Colors.white,
                        fontWeight: FontWeight.w900,
                        letterSpacing: 2,
                      ),
                ),

                const SizedBox(height: 14),

                const Text(
                  'Smart Distributed Acoustic Calibration System',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    color: textMuted,
                    fontSize: 16,
                    height: 1.5,
                  ),
                ),

                const SizedBox(height: 40),

                Container(
                  width: 320,
                  padding: const EdgeInsets.symmetric(
                    horizontal: 20,
                    vertical: 18,
                  ),
                  decoration: BoxDecoration(
                    color: panel,
                    borderRadius: BorderRadius.circular(24),
                  ),
                  child: Column(
                    children: [
                      const Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(
                            Icons.cloud_done_outlined,
                            color: accentLight,
                            size: 18,
                          ),
                          SizedBox(width: 8),
                          Text(
                            'Connecting to Backend',
                            style: TextStyle(
                              color: Colors.white,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ],
                      ),

                      const SizedBox(height: 18),

                      ClipRRect(
                        borderRadius: BorderRadius.circular(999),
                        child: const LinearProgressIndicator(
                          minHeight: 6,
                          backgroundColor: Color(0xFF2A2A36),
                          valueColor: AlwaysStoppedAnimation(
                            accent,
                          ),
                        ),
                      ),

                      const SizedBox(height: 12),

                      const Text(
                        'Initializing acoustic services...',
                        style: TextStyle(
                          color: textMuted,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ),
                ),

                const SizedBox(height: 32),

                const Text(
                  'Version 1.0 Prototype',
                  style: TextStyle(
                    color: textMuted,
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}