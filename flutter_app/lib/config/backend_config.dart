// SDACS Flutter component: Runtime backend address model/controller; defaults to the Raspberry Pi API and supports operator overrides.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/widgets.dart';

class BackendConfig {
  const BackendConfig({
    this.backendIp = defaultBackendIp,
    this.backendPort = defaultBackendPort,
  });

  static const String defaultBackendIp = String.fromEnvironment(
    'SDACS_BACKEND_IP',
    defaultValue: '192.168.5.61',
  );
  static const int defaultBackendPort = int.fromEnvironment(
    'SDACS_BACKEND_PORT',
    defaultValue: 8000,
  );

  final String backendIp;
  final int backendPort;

  String get baseUrl => 'http://$backendIp:$backendPort';

  String get websocketUrl => 'ws://$backendIp:$backendPort/ws/sdacs/live';

  @override
  bool operator ==(Object other) {
    return other is BackendConfig &&
        other.backendIp == backendIp &&
        other.backendPort == backendPort;
  }

  @override
  int get hashCode => Object.hash(backendIp, backendPort);

  BackendConfig copyWith({String? backendIp, int? backendPort}) {
    return BackendConfig(
      backendIp: backendIp ?? this.backendIp,
      backendPort: backendPort ?? this.backendPort,
    );
  }
}

class BackendConfigController extends ChangeNotifier {
  BackendConfigController([BackendConfig config = const BackendConfig()])
    : _config = config;

  BackendConfig _config;

  BackendConfig get config => _config;

  void update(BackendConfig config) {
    if (_config.backendIp == config.backendIp &&
        _config.backendPort == config.backendPort) {
      return;
    }

    _config = config;
    notifyListeners();
  }

  void updateAddress({
    required String backendIp,
    int backendPort = BackendConfig.defaultBackendPort,
  }) {
    final normalizedIp = backendIp.trim();
    if (normalizedIp.isEmpty) {
      return;
    }

    update(BackendConfig(backendIp: normalizedIp, backendPort: backendPort));
  }
}

class BackendConfigScope extends InheritedNotifier<BackendConfigController> {
  const BackendConfigScope({
    super.key,
    required BackendConfigController controller,
    required super.child,
  }) : super(notifier: controller);

  static BackendConfig configOf(BuildContext context) {
    return controllerOf(context).config;
  }

  static BackendConfigController controllerOf(
    BuildContext context, {
    bool listen = true,
  }) {
    final scope = listen
        ? context.dependOnInheritedWidgetOfExactType<BackendConfigScope>()
        : context
                  .getElementForInheritedWidgetOfExactType<BackendConfigScope>()
                  ?.widget
              as BackendConfigScope?;
    assert(scope != null, 'No BackendConfigScope found in context.');
    return scope!.notifier!;
  }
}
