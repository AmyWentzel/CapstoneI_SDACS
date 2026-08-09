// SDACS Flutter verification: regression coverage for room layout test.
// These tests protect operator-visible behavior during the final branch merge.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_test_app/config/backend_config.dart';
import 'package:flutter_test_app/models/room_layout.dart';
import 'package:flutter_test_app/services/sdacs_api_service.dart';

void main() {
  test('default node positions are distinct and non-overlapping anchors', () {
    final layout = RoomLayout.defaults();
    expect(layout.positions.keys.toSet(), roomNodeIds.toSet());
    final points = layout.positions.values
        .map((position) => (position.normalizedX, position.normalizedY))
        .toSet();
    expect(points.length, 4);
  });

  test(
    'room dimensions convert normalized positions to geometric distance',
    () {
      final layout = RoomLayout.defaults().copyWith(
        roomWidthM: 5,
        roomDepthM: 4,
      );
      final node = layout.positions['node01']!;
      expect(node.normalizedX * layout.roomWidthM!, closeTo(.8, .0001));
      expect(node.normalizedY * layout.roomDepthM!, closeTo(.72, .0001));
      expect(layout.layoutDistance('node01'), greaterThan(0));
    },
  );

  test('JSON round trip preserves normalized positions and revision', () {
    final original = RoomLayout.defaults().copyWith(
      roomWidthM: 6,
      roomDepthM: 5,
      revision: 3,
    );
    final restored = RoomLayout.fromJson(original.toJson());
    expect(restored.revision, 3);
    expect(restored.positions['node04']!.normalizedX, .84);
    expect(restored.roomDepthM, 5);
  });

  test('source position is mutable and survives JSON round trip', () {
    final moved = RoomLayout.defaults().copyWith(
      sourcePosition: const SpatialPosition(
        normalizedX: .62,
        normalizedY: .41,
        zM: 1.4,
      ),
    );
    final restored = RoomLayout.fromJson(moved.toJson());
    expect(restored.sourcePosition.normalizedX, .62);
    expect(restored.sourcePosition.normalizedY, .41);
    expect(restored.sourcePosition.zM, 1.4);
  });

  test('layout API 404 has a deployment-specific message', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) {
      request.response.statusCode = HttpStatus.notFound;
      request.response.write('Not Found');
      request.response.close();
    });
    final service = SdacsApiService(
      config: BackendConfig(
        backendIp: InternetAddress.loopbackIPv4.address,
        backendPort: server.port,
      ),
    );
    await expectLater(
      service.getRoomLayout(),
      throwsA(
        isA<SdacsApiException>().having(
          (error) => error.message,
          'message',
          'Room layout API is not available on the deployed backend.',
        ),
      ),
    );
    await subscription.cancel();
    await server.close(force: true);
  });
}
