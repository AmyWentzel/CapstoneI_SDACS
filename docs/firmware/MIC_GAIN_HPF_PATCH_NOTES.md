# feature/mic-gain-HPF Patch Notes

## Implemented architecture

- **Unfiltered spectral path:** signed 24-bit conversion, fixed 8x gain, RMS/FFT/band metrics, acoustic map, and low/mid/high room-band analysis.
- **Scene-classifier path:** signed 24-bit conversion, 150 Hz fourth-order Butterworth HPF, fixed 16x gain, parallel RMS/FFT/band metrics, and Edge Impulse quiet/speech/noisy input.
- **No AGC:** fixed preprocessing is preserved across training and inference.

## Firmware changes

- Added `hpf_filter.c/.h` with two cascaded high-pass biquads.
- Added synchronized dual-path I2S sample output.
- Added independent FFT/RMS contexts for spectral and scene paths.
- Preserved legacy unfiltered MQTT fields.
- Added `scene_*`, HPF configuration, post-HPF level, peak, and separate clipping diagnostics.
- Increased the maximum feature JSON payload to 16 KiB.
- Updated firmware version to `mic-gain-hpf-v2`.

## Backend changes

- Telemetry model accepts both paths and all validation diagnostics.
- `acoustic_input.csv` exports unfiltered and `scene_*` columns.
- Low/mid/high acoustic analysis continues to use unfiltered fields.
- The 57-feature Edge Impulse window builder prefers valid `scene_*` fields and falls back to unfiltered fields for legacy captures.
- AI artifacts identify the dual processing paths and the new feature schema.

## Required next action

Flash node03 first, repeat quiet/close-speech/loud-speech/noisy validation, and confirm scene separation and zero clipping. Recollect and retrain Edge Impulse only after the fixed settings pass.

The existing deployed Edge Impulse model was trained on the previous feature distribution and is not valid for evaluating this patch.

## Rename the Git branch

The supplied archive does not contain `.git` metadata, so the archive itself cannot rename the remote branch. In the real repository, run:

```bash
git switch feature/mic-software-gain
git branch -m feature/mic-gain-HPF
git push origin -u feature/mic-gain-HPF
git push origin --delete feature/mic-software-gain
```

Delete the old remote branch only after the renamed branch has been pushed and verified.
