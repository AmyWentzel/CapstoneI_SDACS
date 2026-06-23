class BackendConfig {
  const BackendConfig({
    this.backendIp = defaultBackendIp,
    this.backendPort = defaultBackendPort,
  });

  static const String defaultBackendIp = String.fromEnvironment(
    'SDACS_BACKEND_IP',
    defaultValue: '192.168.5.40',
  );
  static const int defaultBackendPort = int.fromEnvironment(
    'SDACS_BACKEND_PORT',
    defaultValue: 8000,
  );

  final String backendIp;
  final int backendPort;

  String get baseUrl => 'http://$backendIp:$backendPort';

  String get websocketUrl => 'ws://$backendIp:$backendPort/ws/sdacs/live';
}
