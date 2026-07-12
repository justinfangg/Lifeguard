# AI Lifeguard — Architecture & Design Notes

## Goal

Detect a **distressed / drowning swimmer** in a pool from recorded video
footage (e.g. MP4), using **OpenCV** and **TensorFlow Lite**, and raise a timely
alert. It runs entirely in software on a development machine — no camera or
target board.

## Constraints that shape the design

1. **CPU-only inference.** Models are small and **int8-quantized** and run with
   the **XNNPACK** delegate, so the pipeline stays fast on a plain CPU.
2. **Behaviour over time.** Drowning is a behaviour, not a single-frame object,
   so the pipeline tracks each swimmer and scores distress across a temporal
   window (see below).
3. **Decode / inference decoupling.** Video decode and inference run on separate
   threads with a drop-oldest ring buffer so a slow inference frame never stalls
   decoding.

## Runtime structure

```
 ┌──────────────┐   push (drop-oldest)   ┌───────────────────────────┐
 │ Decode       │ ────────────────────► │ RingBuffer<Frame,4>       │
 │ thread       │                        └───────────────────────────┘
 │ VideoSource  │                                    │ pop
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
| `VideoSource` | `video_source.*` | Decode frames from a recorded video file (MP4) via OpenCV |
| `RingBuffer` | `ring_buffer.hpp` | Lock-free SPSC, drop-oldest frame hand-off |
| `Preprocess` | `preprocess.*` | Letterbox/resize/colour/quantize into model input |
| `Detector` | `detector.*` | TFLite person/swimmer detection (XNNPACK, int8) |
| `Tracker` | `tracker.*` | IOU/SORT-lite ID association + centroid history |
| `PoseEstimator` | `pose_estimator.*` | TFLite MoveNet 17-keypoint pose per swimmer |
| `DistressAnalyzer` | `distress_analyzer.*` | Temporal, rule-based distress scoring + debounce |
| `Alerter` | `alerter.*` | Log / (future) network alert with cooldown |

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

- **v1:** Video-file input, generic detector + MoveNet + rule-based analyzer,
  log alerts.
- **v2:** Fine-tuned detector on pool footage; replace/augment the rule scorer
  with a small temporal classifier (1D-CNN/LSTM over keypoint sequences).
- **v3:** Network alerting to a lifeguard station; watchdog + auto-restart
  hardening.

## Running the pipeline

Set `video_file = <clip>` in the config to run the entire pipeline against
recorded footage. OpenCV decodes the file and the rest of the pipeline is
identical regardless of the source clip.
