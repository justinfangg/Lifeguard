# AI Lifeguard — Architecture & Design Notes

## Goal

Detect a **distressed / drowning swimmer** in a pool from a live camera feed on
a Raspberry Pi 5 running **QNX SDP 8.0**, using **OpenCV** and **TensorFlow
Lite** from oss.qnx.com, and raise a timely alert.

## Constraints that shape the design

1. **CPU-only inference.** Under QNX on the Pi there is no practical GPU/NPU
   offload for TFLite, so models must be small and **int8-quantized** and run
   with the **XNNPACK** delegate.
2. **Camera capture is the hard part.** The Camera Module 3 (IMX708) is a MIPI
   CSI-2 sensor. On the Pi 5 the CSI path goes through the RP1 southbridge, and
   there is no drop-in QNX capture driver. **v1 uses a USB/UVC camera** through
   the QNX camera framework; the CSI path is a later milestone.
3. **Real-time budget.** Target ~15 FPS. Capture and inference run on separate
   threads with a drop-oldest ring buffer so a slow inference frame never
   stalls capture.

## Runtime structure

```
 ┌──────────────┐   push (drop-oldest)   ┌───────────────────────────┐
 │ Capture      │ ─────────────────────► │ RingBuffer<Frame,4>       │
 │ thread       │                        └───────────────────────────┘
 │ CameraSource │                                    │ pop
 └──────────────┘                                    ▼
                                          ┌───────────────────────────┐
                                          │ Inference thread (main)   │
                                          │  Detector  → Tracker      │
                                          │  → PoseEstimator          │
                                          │  → DistressAnalyzer       │
                                          │  → Alerter                │
                                          └───────────────────────────┘
```

## Components

| Component | File | Responsibility |
|-----------|------|----------------|
| `CameraSource` | `camera_source.*` | Abstract capture; `file` (test), `uvc` (v1), `csi` (later) backends |
| `RingBuffer` | `ring_buffer.hpp` | Lock-free SPSC, drop-oldest frame hand-off |
| `Preprocess` | `preprocess.*` | Letterbox/resize/colour/quantize into model input |
| `Detector` | `detector.*` | TFLite person/swimmer detection (XNNPACK, int8) |
| `Tracker` | `tracker.*` | IOU/SORT-lite ID association + centroid history |
| `PoseEstimator` | `pose_estimator.*` | TFLite MoveNet 17-keypoint pose per swimmer |
| `DistressAnalyzer` | `distress_analyzer.*` | Temporal, rule-based distress scoring + debounce |
| `Alerter` | `alerter.*` | Log / GPIO / (future) network alert with cooldown |

## Why a two-stage detection + pose + temporal design?

Drowning is a **behaviour over time**, not an object in a single frame. The
**Instinctive Drowning Response** is quiet and brief (20–60 s). We therefore:

1. **Detect** each swimmer (generic person detector — no public "drowning"
   detector exists).
2. **Track** them so behaviour can be measured per-identity over time.
3. **Estimate pose** to read body orientation and head position.
4. **Score temporally**, combining interpretable features and only alerting
   when the condition **persists** (debouncing cuts false alarms from normal
   treading/floating).

### Distress features (see `DistressAnalyzer::computeInstantScore`)

| Feature | Signal | Weight |
|---------|--------|--------|
| Vertical torso | shoulder→hip vector near vertical | 0.35 |
| Head low | nose at/under shoulder line (mouth at water) | 0.25 |
| No forward progress | small horizontal displacement over window | 0.20 |
| Bobbing | vertical oscillation of the centroid | 0.20 |

These weights and thresholds are **starting points**. They must be calibrated on
real footage for your pool (camera angle, height, water reflections, glare and
refraction are the main failure modes).

## Evolution path

- **v1:** UVC camera, generic detector + MoveNet + rule-based analyzer, log/GPIO
  alerts.
- **v2:** Fine-tuned detector on pool footage; replace/augment the rule scorer
  with a small temporal classifier (1D-CNN/LSTM over keypoint sequences).
- **v3:** Native CSI (IMX708) capture; multi-camera coverage; network alerting
  to a lifeguard station; watchdog + auto-restart hardening.

## Testing without hardware

Set `camera_backend = file` and `video_file = <clip>` in the config to run the
entire pipeline against recorded footage on a dev machine (where OpenCV +
TFLite are available), before cross-compiling for QNX.
