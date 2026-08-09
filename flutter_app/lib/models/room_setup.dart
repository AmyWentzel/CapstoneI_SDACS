// SDACS Flutter component: Setup-screen model for operator-entered room configuration.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

class RoomSetup {
  const RoomSetup({
    required this.roomLength,
    required this.roomWidth,
    required this.roomHeight,
    required this.wallMaterial,
    required this.numberOfNodes,
    required this.wifiSsid,
    required this.wifiPassword,
    required this.backendIp,
  });

  final double roomLength;
  final double roomWidth;
  final double roomHeight;
  final String wallMaterial;
  final int numberOfNodes;
  final String wifiSsid;
  final String wifiPassword;
  final String backendIp;
}
