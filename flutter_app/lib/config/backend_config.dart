class BackendConfig {
  const BackendConfig({
    this.backendIp = defaultBackendIp,
    this.nodeRedPort = defaultNodeRedPort,
  });

  static const String defaultBackendIp = '192.168.1.50';
  static const int defaultNodeRedPort = 1880;

  final String backendIp;
  final int nodeRedPort;

  String get baseUrl => 'http://$backendIp:$nodeRedPort';

  String get websocketUrl => 'ws://$backendIp:$nodeRedPort/ws/sdacs/live';
}
