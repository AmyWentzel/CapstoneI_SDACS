import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../config/backend_config.dart';
import '../../services/sdacs_api_service.dart';
import '../../widgets/sdacs_logo.dart';

class LoadingScreen extends StatefulWidget {
  const LoadingScreen({
    super.key,
    this.autoStart = true,
    this.initialize,
    this.splashDuration = startupSplashDuration,
  });

  static const startupSplashDuration = Duration(seconds: 7);

  final bool autoStart;
  final Future<bool> Function()? initialize;
  final Duration splashDuration;

  @override
  State<LoadingScreen> createState() => _LoadingScreenState();
}

class _LoadingScreenState extends State<LoadingScreen> {
  Timer? _splashTimer;
  bool _splashElapsed = false;
  bool _initializationComplete = false;
  bool _initializationSucceeded = false;
  bool _didNavigate = false;

  @override
  void initState() {
    super.initState();
    if (widget.autoStart) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _startApp());
    }
  }

  Future<void> _startApp() async {
    _splashTimer ??= Timer(widget.splashDuration, () {
      if (!mounted) return;
      setState(() => _splashElapsed = true);
      _navigateIfReady();
    });

    var succeeded = false;
    try {
      final initialize = widget.initialize;
      if (initialize != null) {
        succeeded = await initialize();
      } else {
        final config = BackendConfigScope.configOf(context);
        succeeded = await SdacsApiService(config: config).checkBackendHealth();
      }
    } catch (_) {
      succeeded = false;
    }

    if (!mounted) return;
    setState(() {
      _initializationComplete = true;
      _initializationSucceeded = succeeded;
    });
    _navigateIfReady();
  }

  void _navigateIfReady() {
    if (!mounted ||
        _didNavigate ||
        !_splashElapsed ||
        !_initializationComplete ||
        !_initializationSucceeded) {
      return;
    }
    _didNavigate = true;
    Navigator.of(context).pushReplacementNamed(AppRoutes.main);
  }

  @override
  void dispose() {
    _splashTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;

    return Scaffold(
      body: LayoutBuilder(
        builder: (context, constraints) {
          final logoSize = (constraints.biggest.shortestSide * 0.42).clamp(
            150.0,
            280.0,
          );
          return Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(24),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 560),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    SdacsLogo(
                      size: logoSize,
                      showTitle: true,
                      showFullName: true,
                    ),
                    const SizedBox(height: 28),
                    Semantics(
                      label:
                          _initializationComplete && !_initializationSucceeded
                          ? 'SDACS connection failed'
                          : 'SDACS startup loading',
                      liveRegion: true,
                      child: const SizedBox.square(
                        dimension: 26,
                        child: CircularProgressIndicator(strokeWidth: 3),
                      ),
                    ),
                    const SizedBox(height: 14),
                    Text(
                      _initializationComplete && !_initializationSucceeded
                          ? 'Unable to connect to the SDACS broker.'
                          : 'Connecting to the SDACS broker...',
                      textAlign: TextAlign.center,
                      style: textTheme.bodyMedium,
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}
