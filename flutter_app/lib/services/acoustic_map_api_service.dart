// SDACS Flutter component: HTTP client for acoustic-map endpoints and image/result retrieval.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config/backend_config.dart';
import '../models/acoustic_map_model.dart';

class AcousticMapApiService {
  const AcousticMapApiService({required this.config});

  final BackendConfig config;

  Future<AcousticMapResult> getMap({String label = 'latest'}) async {
    final uri = Uri.parse(
      '${config.baseUrl}/api/acoustic-map',
    ).replace(queryParameters: {'label': label});
    final response = await http.get(uri).timeout(const Duration(seconds: 10));
    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw Exception(
        'Acoustic map failed: HTTP ${response.statusCode} ${response.body}',
      );
    }
    final decoded = jsonDecode(response.body);
    if (decoded is! Map<String, dynamic>) {
      throw const FormatException(
        'Acoustic-map response was not a JSON object.',
      );
    }
    return AcousticMapResult.fromJson(decoded);
  }

  Uri imageUri(String label, {int? cacheBust}) {
    return Uri.parse('${config.baseUrl}/api/acoustic-map/image').replace(
      queryParameters: {
        'label': label,
        'v': (cacheBust ?? DateTime.now().millisecondsSinceEpoch).toString(),
      },
    );
  }
}
