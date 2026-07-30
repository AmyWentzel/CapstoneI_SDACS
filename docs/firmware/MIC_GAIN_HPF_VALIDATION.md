# SDACS Dual-Path Microphone Gain and HPF Validation

Firmware version: `mic-gain-hpf-v1`

## Purpose

This patch separates frequency-condition analysis from acoustic-scene classification so bass suppression does not destroy the information needed by the `low`, `mid`, and `high` analysis path.

## Processing architecture

```text
ICS-43432 32-bit I2S word
        |
        +--> signed 24-bit conversion --> fixed 8x gain --> unfiltered spectral metrics
        |                                                    |
        |                                                    +--> low / mid / high room-band analysis
        |
        +--> signed 24-bit conversion --> 150 Hz 4th-order HPF --> fixed 16x gain
                                                             |
                                                             +--> quiet / speech / noisy Edge Impulse features
```

Automatic gain control is intentionally not used. Every training and validation capture must use the same fixed filter and gain settings.

## Firmware configuration

Edit `main/sdacs_config.h` only after a controlled node03 validation:

```c
#define SDACS_MIC_SOFTWARE_GAIN       8.0f
#define SDACS_SCENE_HPF_ENABLED       1
#define SDACS_SCENE_HPF_CUTOFF_HZ     150.0f
#define SDACS_SCENE_HPF_ORDER         4
#define SDACS_SCENE_SOFTWARE_GAIN     16.0f
```

The HPF is implemented as two cascaded Butterworth biquads. At 48 kHz, the expected approximate response is:

| Frequency | Expected gain |
|---:|---:|
| 46.9 Hz | -40.4 dB |
| 93.8 Hz | -16.4 dB |
| 140.6 Hz | -4.3 dB |
| 150 Hz | -3.0 dB |
| 250 Hz | -0.1 dB |
| 1 kHz | approximately 0 dB |

## MQTT fields

The original fields remain the unfiltered spectral path for compatibility. New scene-classifier fields are prefixed with `scene_`.

Key validation fields:

```text
fw_version
mic_software_gain
scene_hpf_enabled
scene_hpf_cutoff_hz
scene_hpf_order
scene_software_gain

dbfs
fft_low_ratio
fft_mid_ratio
fft_high_ratio

scene_dbfs
scene_fft_low_ratio
scene_fft_mid_ratio
scene_fft_high_ratio
scene_fft_total_energy

pre_gain_dbfs
post_hpf_dbfs
scene_current_dbfs
post_gain_peak_abs
scene_post_gain_peak_abs
spectral_clipped_sample_count
scene_clipped_sample_count
```

## Controlled node03 validation

Place the speaker 30 to 50 cm from node03 and keep position, orientation, playback device, and volume fixed.

Run four 20-second captures:

1. Quiet room.
2. Continuous close speech.
3. Loud close speech.
4. Representative noisy playback.

Monitor the scene path:

```bash
mosquitto_sub -h localhost -t 'sdacs/node/node03/features' |
python3 -c '
import json, sys
for line in sys.stdin:
    d = json.loads(line)
    print(
        "seq=", d.get("seq"),
        "spectral_dbfs=", d.get("dbfs"),
        "scene_dbfs=", d.get("scene_dbfs"),
        "post_hpf_dbfs=", d.get("post_hpf_dbfs"),
        "scene_peak=", d.get("scene_post_gain_peak_abs"),
        "scene_clipped=", d.get("scene_clipped_sample_count"),
        "scene_low=", d.get("scene_fft_low_ratio"),
        "scene_mid=", d.get("scene_fft_mid_ratio"),
        "scene_high=", d.get("scene_fft_high_ratio")
    )
'
```

## Acceptance criteria before full recollection

| Check | Initial target |
|---|---:|
| Close-speech mean scene level | at least -63 dBFS |
| Quiet-to-close-speech separation | at least 5 to 6 dB |
| Scene clipped samples | 0 |
| Spectral clipped samples | 0 |
| Effective sample rate | approximately 48 kHz and `sample_rate_ok=true` |
| HPF settings | identical on every node and every capture |
| Quiet low-frequency scene ratio | materially lower than the unfiltered quiet ratio |

The -63 dBFS level is a validation target, not a guaranteed result. The meaningful classifier requirement is stable separation between labels without clipping.

## Edge Impulse rule

The deployed pre-HPF model must not be used as a pass/fail authority after this patch. Recollect the `quiet_room_white_noise`, `speech`, and `noisy` dataset with `mic-gain-hpf-v1`, rebuild the 57-feature windows from `scene_*` fields, and retrain Edge Impulse.

The unfiltered path remains available for low/mid/high frequency-condition analysis and acoustic mapping.
