// SDACS Flutter component: Named navigation routes for the SDACS Flutter application.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

import '../screens/calibration/calibration_screen.dart';
import '../screens/graphs/graphs_screen.dart';
import '../screens/loading/loading_screen.dart';
import '../screens/main/main_screen.dart';
import '../screens/operator_tools/operator_tools_screen.dart';
import '../screens/setup/setup_screen.dart';

class AppRoutes {
  const AppRoutes._();

  static const String loading = '/loading';
  static const String main = '/main';
  static const String setup = '/setup';
  static const String operatorTools = '/operator-tools';
  static const String calibration = '/calibration';
  static const String graphs = '/graphs';
  static const String captureResults = '/capture-results';

  static Map<String, WidgetBuilder> get routes {
    return {
      loading: (context) => const LoadingScreen(),
      main: (context) => const MainScreen(),
      setup: (context) => const SetupScreen(),
      operatorTools: (context) => const OperatorToolsScreen(),
      calibration: (context) => const CalibrationScreen(),
      graphs: (context) => const GraphsScreen(),
      captureResults: (context) => const GraphsScreen(),
    };
  }
}
