# AI Lifeguard

A software application that watches recorded swimming-pool footage and raises an
alert when it detects a **distressed / drowning swimmer**.

It runs a computer-vision pipeline built on **OpenCV** and **TensorFlow Lite**,
analyzing video files (e.g. MP4) frame by frame. It is a pure host application —
no camera hardware and no target board required.

> ⚠️ **Safety notice.** This is experimental software and **must not** be relied
> upon as a substitute for a trained human lifeguard. Treat any alert as an
> assist, never as a guarantee.

---

## How it works

```
 MP4 file ─► Decode ─► Real-time pacing ─► Pre-process ─► Detect swimmers
                                                              │
                                                              ▼
   Alarm ◄─ Distress logic ◄─ Temporal window ◄─ Pose / behaviour
```

| Stage | Component | Tech |
|-------|-----------|------|
| Decode | `VideoSource` | OpenCV `VideoCapture` (MP4 / video file) |
| Buffering | `RingBuffer` | lock-free SPSC queue |
| Pre-process | `Preprocess` | OpenCV resize / normalize |
| Detect swimmers | `Detector` | TFLite (MobileNet-SSD / nano-YOLO, int8) |
| Track | `Tracker` | IOU/SORT-style ID association |
| Pose | `PoseEstimator` | TFLite MoveNet Lightning (int8) |
| Behaviour | `DistressAnalyzer` | temporal rule-based scorer + debounce |
| Alert | `Alerter` | log / network event |

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design and the
reasoning behind the drowning-detection approach.

## Distress detection approach

Drowning rarely looks like the movies. The **Instinctive Drowning Response** is
near-silent and lasts only 20–60 seconds. We look for a *combination* of signals
sustained over several seconds for the same tracked person:

- torso close to **vertical** in the water,
- **head low** / tilted back, mouth at the water line,
- **little or no forward progress** (low horizontal velocity),
- arms pressing **laterally** rather than stroking,
- repeated **bobbing** (vertical oscillation).

A single frame never triggers an alert — the `DistressAnalyzer` requires the
condition to persist across a sliding time window (debouncing) to keep false
alarms from normal treading/floating low.

## Repository layout

```
Lifeguard/
├── CMakeLists.txt            Top-level build
├── include/lifeguard/        Public headers (one per pipeline stage)
├── src/                      Implementations + main.cpp
├── config/lifeguard.conf     Runtime configuration
├── models/                   TFLite models (not committed – see scripts)
├── scripts/                  Model fetch helper
└── docs/ARCHITECTURE.md      Design notes
```

> See [`docs/BUILD.md`](docs/BUILD.md) for the full build + run workflow.

## Building

Prerequisites:

- A C++17 compiler and CMake 3.16+
- **OpenCV** 4 or newer (`brew install opencv`, `apt install libopencv-dev`, …)
- **TensorFlow Lite** — point CMake at a TensorFlow source tree
  (`-DTFLITE_SOURCE_DIR=<tensorflow>`) or an installed prefix
  (`-DTFLITE_ROOT=<prefix>`).

```bash
# 1. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DTFLITE_SOURCE_DIR=/path/to/tensorflow \
      -DTENSORFLOW_OFFLINE=OFF

# 2. Build
cmake --build build -j

# 3. Fetch the detector model
sh scripts/fetch_models.sh
```

The normal build downloads TensorFlow Lite's pinned CMake dependencies
(Abseil, protobuf, FlatBuffers, XNNPACK, and related libraries). Use
`-DTENSORFLOW_OFFLINE=ON` only when those exact dependencies have already been
populated locally; stale dependency copies can produce version-mismatch errors.

## Running

```bash
# Point video_file in config/lifeguard.conf at your footage, then:
./build/ai_lifeguard --config config/lifeguard.conf
```

The app decodes the video file, runs the detector and MoveNet pose model,
displays swimmer and distress overlays in an OpenCV window, and writes the
annotated stream to `output_video` (default
`videos/lifeguard_annotated.mp4`). Alerts are also logged to `log_path`
(default `lifeguard.log`). Press `q` or `Esc` to stop.

For a headless smoke test, set `display = false` in the config. The application
fails early with a clear message if the video or detector model is missing.

### Calibration

The detector and distress thresholds are configurable:

```ini
detector_score_threshold = 0.5
distress_score_threshold = 0.6
distress_persist_seconds = 4.0
temporal_window_seconds  = 6.0
person_class_id          = 1
```

Review the red-box overlays on representative pool footage, then adjust the
thresholds to balance swimmer recall and alert frequency. This remains an
experimental rule-based aid, not a validated drowning detector.

## Roadmap / status

- [x] Project scaffold, build system, pipeline interfaces
- [x] Video-file input via OpenCV
- [ ] Integrate TFLite detector (XNNPACK delegate, int8)
- [ ] Tracker + MoveNet pose + temporal distress logic
- [ ] Alerting (log / network event)
- [ ] Calibration & false-alarm tuning
