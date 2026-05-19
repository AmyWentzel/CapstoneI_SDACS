import 'package:flutter/material.dart';

import 'app_routes.dart';
import 'app_theme.dart';

class SdacsApp extends StatelessWidget {
  const SdacsApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SDACS',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light,
      darkTheme: AppTheme.dark,
      themeMode: ThemeMode.system,
      initialRoute: AppRoutes.loading,
      routes: AppRoutes.routes,
    );
  }
}
