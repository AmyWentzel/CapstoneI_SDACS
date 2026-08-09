// SDACS Flutter component: Compact model for displayed calibration outcomes.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

class CalibrationResult {
  const CalibrationResult({
    required this.calibrationId,
    required this.status,
    required this.startedAt,
    this.completedAt,
    required this.referenceFrequencyHz,
    required this.referenceSplDb,
    required this.measuredSplDb,
    required this.calibrationOffsetDb,
  });

  final String calibrationId;
  final String status;
  final DateTime startedAt;
  final DateTime? completedAt;
  final double referenceFrequencyHz;
  final double referenceSplDb;
  final double measuredSplDb;
  final double calibrationOffsetDb;

  factory CalibrationResult.placeholder() {
    final now = DateTime.now();
    return CalibrationResult(
      calibrationId: 'cal-placeholder',
      status: 'ready',
      startedAt: now.subtract(const Duration(minutes: 12)),
      completedAt: now.subtract(const Duration(minutes: 10)),
      referenceFrequencyHz: 1000,
      referenceSplDb: 94,
      measuredSplDb: 92.7,
      calibrationOffsetDb: 1.3,
    );
  }
}
