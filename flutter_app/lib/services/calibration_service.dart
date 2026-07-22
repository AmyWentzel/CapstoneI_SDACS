import '../models/calibration_result.dart';

class CalibrationService {
  const CalibrationService();

  Future<void> startCalibration({
    required int delayMs,
    required int durationSeconds,
  }) async {
    await Future<void>.delayed(const Duration(milliseconds: 400));
  }

  Future<void> stopCalibration() async {
    await Future<void>.delayed(const Duration(milliseconds: 300));
  }

  Future<CalibrationResult> getLatestCalibration() async {
    await Future<void>.delayed(const Duration(milliseconds: 250));
    return CalibrationResult.placeholder();
  }
}
