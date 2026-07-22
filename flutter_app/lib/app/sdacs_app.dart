import 'package:flutter/material.dart';

import '../config/backend_config.dart';
import 'app_routes.dart';
import 'app_theme.dart';

class SdacsApp extends StatefulWidget {
  const SdacsApp({super.key});

  @override
  State<SdacsApp> createState() => _SdacsAppState();
}

class _SdacsAppState extends State<SdacsApp> {
  final BackendConfigController _backendConfigController =
      BackendConfigController();

  @override
  void dispose() {
    _backendConfigController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return BackendConfigScope(
      controller: _backendConfigController,
      child: MaterialApp(
        title: 'SDACS',
        debugShowCheckedModeBanner: false,
        theme: AppTheme.light,
        darkTheme: AppTheme.dark,
        themeMode: ThemeMode.system,
        initialRoute: AppRoutes.loading,
        routes: AppRoutes.routes,
      ),
    );
  }
}
