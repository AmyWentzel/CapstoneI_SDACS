# SDACS_V3 Edge Impulse deployment

The generated model is stored at `edge_impulse/models/sdacs_v3_v1/`. It is
project 1071949, impulse 1, deploy version 1, with 57 sensor-fusion inputs and
the labels `noisy`, `quiet_room_white_noise`, and `speech`.

The verified feature builder fuses one synchronized `sample_index` from
node01-node04 into 57 ordered values using arithmetic mean, population standard
deviation (`ddof=0`), and max-minus-min range. Capture-level temporal fusion is
not yet validated, so per-window inference must not be presented as one final
capture classification.

On the Raspberry Pi 5:

```bash
cd /home/kyledavid36/sdacs/backend/sdacs_api/edge_impulse/runner
make clean
make -j$(nproc)
../bin/sdacs_ei_runner --health
```

The binary must report project 1071949, deploy version 1, all three labels, and
57 features. Build on the Pi (or explicitly cross-compile for AARCH64); a
Windows executable cannot be deployed. Run the checked-in Studio golden fixture
through the optional runner test before deployment.

For per-window development:

```bash
export SDACS_EI_RUNNER_PATH=/home/kyledavid36/sdacs/backend/sdacs_api/edge_impulse/bin/sdacs_ei_runner
export SDACS_EI_EXPECTED_PROJECT_ID=1071949
export SDACS_EI_EXPECTED_DEPLOY_VERSION=1
export SDACS_EI_TIMEOUT_SECONDS=30
export SDACS_EI_ENABLED=true
sudo systemctl restart sdacs-api
sudo systemctl status sdacs-api --no-pager
journalctl -u sdacs-api -n 100 --no-pager
curl -s http://127.0.0.1:8000/api/ai/health
```

Run the Studio golden fixture before a live capture. Its expected values came
from Studio sample `sdacs_ei_scene_3class_testing.s345`, not the local runner.

For a new normal capture, verify the same capture ID in the API and Flutter,
then inspect `ai_input.json`, `ai_window_predictions.json`, `ai_summary.json`,
and `combined_result.json`. The summary must remain `fusion_not_configured`
until a temporal fusion method is separately evaluated.
Calibration captures must report `not_applicable`.

Rollback:

```bash
export SDACS_EI_ENABLED=false
sudo systemctl restart sdacs-api
```

This preserves historical AI artifacts and all acoustic processing while new
captures return `acoustic_only`.
