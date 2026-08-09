# SDACS Microphone Software Gain Validation

Firmware version: `mic-gain-v1`

## Processing path

```text
32-bit I2S raw word
→ selected left/right slot
→ SHIFT8 or LOW24 signed 24-bit conversion
→ configurable software gain
→ signed 24-bit saturation
→ RMS, dBFS, FFT, band energy, and published AI features
```

## Configuration

Edit `main/sdacs_config.h`:

```c
#define SDACS_MIC_SOFTWARE_GAIN 8.0f
```

- `1.0f` disables gain while retaining the same processing path.
- `8.0f` adds approximately 18.06 dB before feature extraction.
- Gain is applied to PCM samples, not added only to the reported dBFS value.

## Clipping protection

Samples are saturated to the signed 24-bit range:

```text
-8,388,608 to +8,388,607
```

The telemetry diagnostics now include:

- `mic_software_gain`
- `pre_gain_peak_abs`
- `post_gain_peak_abs`
- `clipped_sample_count`
- `pre_gain_rms`
- `pre_gain_dbfs`
- existing `current_*` values, which now represent post-gain production samples

## Validation sequence

1. Build and flash one node first.
2. Capture a quiet-room sample.
3. Capture a fixed 1 kHz tone at a stable speaker level and distance.
4. Confirm `post_gain_peak_abs` is approximately gain × `pre_gain_peak_abs` when not clipped.
5. Confirm `clipped_sample_count` remains zero. Reduce gain if clipping occurs.
6. Repeat on all four nodes using the same setup.
7. Recalibrate SPL and regenerate/retrain the Edge Impulse dataset after the production gain is finalized.

The current Edge Impulse model was trained on the previous feature scale and should not be treated as valid after changing PCM gain.
