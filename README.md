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
 MP4 file ─► Decode ─► Ring buffer ─► Pre-process ─► Detect swimmers
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
      -DTFLITE_SOURCE_DIR=/path/to/tensorflow

# 2. Build
cmake --build build -j

# 3. Fetch the detector model
sh scripts/fetch_models.sh
```

## Running

```bash
# Point video_file in config/lifeguard.conf at your footage, then:
./build/ai_lifeguard --config config/lifeguard.conf
```

The app decodes the video file, runs detection + tracking + distress analysis,
and logs alerts to `log_path` (default `lifeguard.log`). It exits when the
video reaches end-of-stream.

## Roadmap / status

- [x] Project scaffold, build system, pipeline interfaces
- [x] Video-file input via OpenCV
- [ ] Integrate TFLite detector (XNNPACK delegate, int8)
- [ ] Tracker + MoveNet pose + temporal distress logic
- [ ] Alerting (log / network event)
- [ ] Calibration & false-alarm tuning
