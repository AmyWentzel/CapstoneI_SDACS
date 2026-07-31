# SDACS SPL Calibration Workflow

## Purpose

This workflow converts each node's measured digital level into an estimated sound-pressure level using a persistent additive firmware offset:

```text
dB SPL = dBFS + cal_offset_db
suggested cal_offset_db = reference meter dB SPL - median captured dBFS
```

The backend, rather than the Flutter client, performs the authoritative calculation and safety checks.

## Required setup

1. Temporarily co-locate the microphones of node01 through node04 in a tight cluster beside the reference sound-level meter. Keep the microphones at approximately the same height and distance from the loudspeaker. Do not calculate four calibration offsets while the nodes are spread around the room, because real spatial SPL differences would be mistaken for sensor error.
2. Document the meter weighting, time response, range, loudspeaker level, distance, and room conditions. A steady 1 kHz tone is normally measured with the meter's documented settings kept unchanged for both captures.
3. Play the supplied continuous 1 kHz WAV through the studio monitor for the synchronized 60-second capture.
4. Do not change the monitor level, clustered microphone positions, or meter position between the calibration capture and the verification capture. Move the nodes to their intended room positions only after verification passes.

## Operator Tools sequence

1. Open **Operator Tools**.
2. Under **Calibration Signals**, run **1 kHz Calibration Capture**.
3. When processing finishes, enter the reference meter reading in **SPL Calibration**.
4. Select **Calculate Suggested Offsets**.
5. Review all four node cards. The Apply button remains locked unless every node passes:
   - at least three valid dBFS samples;
   - at least 60% dedicated 1 kHz detector success;
   - representative detector peak from 900 to 1100 Hz;
   - no reported clipping; and
   - suggested firmware offset from 60 to 180 dB.
6. Select **Apply Confirmed Offsets** and confirm the four values.
7. The backend sends an individual `set_cal_offset` command to each node and waits for a matching firmware acknowledgement.
8. Run **1 kHz Verification Capture**. The GUI reports whether all nodes are within ±1.0 dB of the reference reading.

## Backend API

### Preview

`POST /api/calibration/preview`

```json
{
  "capture_id": "capture_...",
  "reference_spl_db": 70.0
}
```

The response includes per-node median dBFS, current offset, suggested offset, adjustment, 1 kHz detection rate, clipping/tone warnings, eligibility, and tolerance status.

### Apply

`POST /api/calibration/apply`

```json
{
  "capture_id": "capture_...",
  "reference_spl_db": 70.0,
  "node_offsets_db": {
    "node01": 119.84,
    "node02": 120.17,
    "node03": 119.65,
    "node04": 120.09
  },
  "allow_partial": false
}
```

The backend only accepts eligible nodes and offsets matching its preview within 0.10 dB. It publishes the following command on each node's command topic:

```json
{
  "cmd": "set_cal_offset",
  "request_id": "cal_..._node01",
  "cal_offset_db": 119.84
}
```

An offset is considered applied only when the firmware returns `result: "ok"` and reports a saved offset within 0.05 dB of the requested value.

### Records

- `GET /api/calibration/latest`
- `GET /api/captures/{capture_id}/calibration`
- Persisted artifact: `calibration_result.json`

## Deployment checks

From the Raspberry Pi backend directory:

```bash
cd /home/kyledavid36/sdacs-source/backend/sdacs_api
python3 -m pip install -r requirements.txt
PYTHONPATH=. pytest -q tests/test_calibration.py
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Confirm the firmware response path manually when needed:

```bash
mosquitto_sub -h 127.0.0.1 -t 'sdacs/node/+/status' -v
```

The response should contain `record_type: "command_response"`, `cmd: "set_cal_offset"`, `result: "ok"`, `reason: "saved"`, the matching request ID, and the reported `cal_offset_db`.

## Important interpretation note

The 1 kHz validation uses the firmware's dedicated tone detector (`tone_1khz_*`), not the broad FFT peak field. This avoids relying on the broad low-frequency peak estimate for the calibration acceptance decision.
