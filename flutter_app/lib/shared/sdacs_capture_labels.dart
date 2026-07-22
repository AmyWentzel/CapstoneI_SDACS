class SdacsCaptureLabelDefinition {
  const SdacsCaptureLabelDefinition({
    required this.id,
    required this.displayName,
    required this.description,
  });

  final String id;
  final String displayName;
  final String description;
}

const sdacsCaptureLabels = <SdacsCaptureLabelDefinition>[
  SdacsCaptureLabelDefinition(
    id: 'noisy',
    displayName: 'Noisy',
    description: 'General high-noise room condition.',
  ),
  SdacsCaptureLabelDefinition(
    id: 'speech',
    displayName: 'Speech',
    description: 'Spoken voice in the monitored room.',
  ),
  SdacsCaptureLabelDefinition(
    id: 'low',
    displayName: 'low (40-500Hz)',
    description: 'Project-defined low-frequency condition.',
  ),
  SdacsCaptureLabelDefinition(
    id: 'mid',
    displayName: 'mid (400-4kHz)',
    description: 'Project-defined mid-frequency condition.',
  ),
  SdacsCaptureLabelDefinition(
    id: 'high',
    displayName: 'high (4k-20kHz)',
    description: 'Project-defined high-frequency condition.',
  ),
  SdacsCaptureLabelDefinition(
    id: 'quiet_room_white_noise',
    displayName: 'Quiet Room (white noise)',
    description: 'Quiet-room baseline capture using white noise.',
  ),
];
